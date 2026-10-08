#include "scenarios.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <string>
#include <utility>
#include <vector>

#include "ballMovement.hpp"
#include "goldenScenarios.hpp"
#include "ids.hpp"
#include "kickoffScenario.hpp"
#include "matchCommand.hpp"
#include "matchSetup.hpp"
#include "matchState.hpp"
#include "passing.hpp"
#include "pitch.hpp"
#include "referenceTactic.hpp"
#include "restart.hpp"
#include "simTime.hpp"
#include "tactic.hpp"
#include "vec2.hpp"

namespace ElyverseFootball::SimMatch {
namespace {

// The sandbox pitch: an example 7v7 size, not a mandated one.
constexpr double kPitchLength = kSandboxLengthMeters;
constexpr double kPitchWidth = kSandboxWidthMeters;

[[nodiscard]] std::expected<MatchSetup, std::string> kickoffWith(const std::uint64_t seed,
                                                                 const SimCore::Vec2 ballVelocity) {
  auto state = makeSevenASideKickoff(Pitch(kPitchLength, kPitchWidth), ballVelocity);
  if (!state) {
    return std::unexpected("invalid kickoff fixture: " + state.error().front().message);
  }
  return MatchSetup{.initialState = *std::move(state), .config = {}, .seed = seed, .commands = {}};
}

[[nodiscard]] std::expected<MatchSetup, std::string> kickoff(const std::uint64_t seed) {
  return kickoffWith(seed, {});
}

[[nodiscard]] std::expected<MatchSetup, std::string> tacticMatch(const std::uint64_t seed) {
  auto reference = SimTactics::Tactic::create(SimTactics::referenceTacticSpec());
  if (!reference) {
    return std::unexpected("invalid reference tactic: " + reference.error().front().message);
  }
  return makeTacticMatch({.home = *reference, .away = *reference}, seed);
}

[[nodiscard]] std::expected<MatchSetup, std::string> touchlineTrap(const std::uint64_t seed) {
  return makePressingTrap(seed, TrapSpot::kTouchline, 1.0);
}

[[nodiscard]] std::expected<MatchSetup, std::string> lonePress(const std::uint64_t seed) {
  return makePressingTrap(seed, TrapSpot::kTouchline, 0.25);
}

[[nodiscard]] std::expected<MatchSetup, std::string> rollingBall(const std::uint64_t seed) {
  return kickoffWith(seed, {.x = 8.0, .y = 3.0});
}

// Player index i (id i + 1) visits waypoint k at the pitch position given by
// a fixed arithmetic pattern: spread over the whole pitch, different for every
// player, no randomness. Waypoints change every 4 to 8 seconds, often before a
// player has arrived, so runs are interrupted and redirected mid-stride.
[[nodiscard]] SimCore::Vec2 m0Waypoint(const std::size_t playerIndex, const std::size_t waypoint) {
  const std::size_t column = ((playerIndex * 7U) + (waypoint * 13U)) % 11U;
  const std::size_t row = ((playerIndex * 5U) + (waypoint * 3U)) % 9U;
  return {.x = 5.0 + (5.0 * static_cast<double>(column)),
          .y = 4.0 + (4.0 * static_cast<double>(row))};
}

// The M0 acceptance scenario of docs/scenarios.md: every player moving, with
// several target changes each, and a rolling ball.
[[nodiscard]] std::expected<MatchSetup, std::string> m0Acceptance(const std::uint64_t seed) {
  auto setup = kickoffWith(seed, {.x = 9.0, .y = 4.0});
  if (!setup) {
    return setup;
  }
  constexpr std::size_t kWaypoints = 6;
  const std::size_t playerCount = setup->initialState.players().size();
  for (std::size_t waypoint = 0; waypoint < kWaypoints; ++waypoint) {
    for (std::size_t index = 0; index < playerCount; ++index) {
      // Waypoint k starts at 6 s · k, staggered by player so changes spread
      // over two seconds instead of landing on one tick.
      const auto tick = static_cast<SimCore::SimTick::ValueType>((waypoint * 180U) + (index * 4U));
      setup->commands.push_back(
          {.tick = SimCore::SimTick(tick),
           .command = MovePlayerCommand{.playerId = setup->initialState.players()[index].playerId,
                                        .target = m0Waypoint(index, waypoint)}});
    }
  }
  // Targets off the pitch are moved onto it: one behind a goal line, one past
  // a corner.
  setup->commands.push_back({.tick = SimCore::SimTick(600),
                             .command = MovePlayerCommand{.playerId = SimCore::PlayerId(1),
                                                          .target = {.x = -8.0, .y = 20.0}}});
  setup->commands.push_back({.tick = SimCore::SimTick(600),
                             .command = MovePlayerCommand{.playerId = SimCore::PlayerId(14),
                                                          .target = {.x = 75.0, .y = 55.0}}});
  return setup;
}

// ---------------------------------------------------------------------------
// P1 passing scenarios: hand-placed fixtures in which the home player 1 gets
// the ball at kickoff and the standard systems -- perception, pass decisions,
// execution, reception -- play on without further commands.

// A hand-placed player: where he stands and which way he faces, a unit
// vector: (+1, 0) toward the away goal, (-1, 0) toward the home goal.
struct Placement {
  double x;
  double y;
  double facingX;
  double facingY = 0.0;
};

using Side = std::array<Placement, kDefaultPlayersPerSide>;

constexpr SimCore::PlayerId kFirstCarrier{1};

// Home players get ids 1 to 7 in order, away players 8 to 14. The ball starts
// at the first carrier's feet and is given to him at tick 0.
[[nodiscard]] std::expected<MatchSetup, std::string> placed(const std::uint64_t seed,
                                                            const Side& home, const Side& away,
                                                            TeamTactics tactics = {}) {
  const Pitch pitch(kPitchLength, kPitchWidth);
  const MatchConfig config;
  std::vector<PlayerMatchState> players;
  players.reserve(home.size() + away.size());
  SimCore::PlayerId::ValueType nextId = 1;
  for (const auto& [side, placements] :
       {std::pair{TeamSide::kHome, &home}, std::pair{TeamSide::kAway, &away}}) {
    for (const Placement& placement : *placements) {
      players.push_back({.playerId = SimCore::PlayerId(nextId++),
                         .side = side,
                         .position = {.x = placement.x, .y = placement.y},
                         .velocity = {},
                         .attributes = {},
                         .target = std::nullopt,
                         .facing = {.x = placement.facingX, .y = placement.facingY}});
    }
  }
  const PlayerMatchState& carrier = players.front();
  const BallState ball{.position = carriedBallPosition(carrier, config.ball, pitch),
                       .velocity = {},
                       .owner = std::nullopt,
                       .lastTouch = std::nullopt};
  auto state = MatchState::create({.pitch = pitch,
                                   .players = std::move(players),
                                   .ball = ball,
                                   .playersPerSide = kDefaultPlayersPerSide},
                                  std::move(tactics));
  if (!state) {
    return std::unexpected("invalid scenario fixture: " + state.error().front().message);
  }
  return MatchSetup{.initialState = *std::move(state),
                    .config = config,
                    .seed = seed,
                    .commands = {{.tick = SimCore::SimTick(0),
                                  .command = GiveBallCommand{.playerId = kFirstCarrier}}}};
}

// Home in a zigzag up the pitch, every player facing the away goal, so each
// carrier sees teammates ahead of him. The away side stands on the pitch just
// in front of the home goal line, behind every pass: its nearest player still
// chases a free ball, but stands too far away to reach a pass first.
[[nodiscard]] std::expected<MatchSetup, std::string> passChain(const std::uint64_t seed) {
  constexpr Side kHome{{{.x = 8.0, .y = 20.0, .facingX = 1.0},
                        {.x = 20.0, .y = 12.0, .facingX = 1.0},
                        {.x = 20.0, .y = 28.0, .facingX = 1.0},
                        {.x = 32.0, .y = 20.0, .facingX = 1.0},
                        {.x = 44.0, .y = 12.0, .facingX = 1.0},
                        {.x = 44.0, .y = 28.0, .facingX = 1.0},
                        {.x = 54.0, .y = 20.0, .facingX = 1.0}}};
  constexpr Side kAway{{{.x = 2.0, .y = 4.0, .facingX = 1.0},
                        {.x = 2.0, .y = 10.0, .facingX = 1.0},
                        {.x = 2.0, .y = 16.0, .facingX = 1.0},
                        {.x = 2.0, .y = 24.0, .facingX = 1.0},
                        {.x = 2.0, .y = 30.0, .facingX = 1.0},
                        {.x = 2.0, .y = 36.0, .facingX = 1.0},
                        {.x = 1.0, .y = 20.0, .facingX = 1.0}}};
  return placed(seed, kHome, kAway);
}

// Player 1's only visible teammate is player 2, 20 m ahead; the rest of the
// home side stands behind him, out of sight. Away player 8 stands 5.8 m off
// the lane between them: the pass is risky but still valid, player 1 plays
// it, and player 8 usually gets there first. The other away players wait far
// beyond player 2.
[[nodiscard]] std::expected<MatchSetup, std::string> interceptedPass(const std::uint64_t seed) {
  constexpr Side kHome{{{.x = 20.0, .y = 20.0, .facingX = 1.0},
                        {.x = 40.0, .y = 20.0, .facingX = -1.0},
                        {.x = 5.0, .y = 5.0, .facingX = 1.0},
                        {.x = 5.0, .y = 35.0, .facingX = 1.0},
                        {.x = 3.0, .y = 15.0, .facingX = 1.0},
                        {.x = 3.0, .y = 25.0, .facingX = 1.0},
                        {.x = 2.0, .y = 20.0, .facingX = 1.0}}};
  constexpr Side kAway{{{.x = 34.0, .y = 25.8, .facingX = -1.0},
                        {.x = 58.0, .y = 5.0, .facingX = -1.0},
                        {.x = 58.0, .y = 35.0, .facingX = -1.0},
                        {.x = 57.0, .y = 15.0, .facingX = -1.0},
                        {.x = 57.0, .y = 25.0, .facingX = -1.0},
                        {.x = 59.0, .y = 20.0, .facingX = -1.0},
                        {.x = 56.0, .y = 2.0, .facingX = -1.0}}};
  return placed(seed, kHome, kAway);
}

// Player 1 faces the away goal with every teammate behind him, beyond his
// awareness radius: he sees no one to pass to and keeps the ball. The away
// side waits in its own half, too far away to matter.
[[nodiscard]] std::expected<MatchSetup, std::string> noPassingOption(const std::uint64_t seed) {
  constexpr Side kHome{{{.x = 30.0, .y = 20.0, .facingX = 1.0},
                        {.x = 20.0, .y = 10.0, .facingX = 1.0},
                        {.x = 20.0, .y = 30.0, .facingX = 1.0},
                        {.x = 15.0, .y = 20.0, .facingX = 1.0},
                        {.x = 10.0, .y = 5.0, .facingX = 1.0},
                        {.x = 10.0, .y = 35.0, .facingX = 1.0},
                        {.x = 3.0, .y = 20.0, .facingX = 1.0}}};
  constexpr Side kAway{{{.x = 45.0, .y = 10.0, .facingX = -1.0},
                        {.x = 45.0, .y = 30.0, .facingX = -1.0},
                        {.x = 50.0, .y = 20.0, .facingX = -1.0},
                        {.x = 55.0, .y = 5.0, .facingX = -1.0},
                        {.x = 55.0, .y = 35.0, .facingX = -1.0},
                        {.x = 58.0, .y = 20.0, .facingX = -1.0},
                        {.x = 52.0, .y = 12.0, .facingX = -1.0}}};
  return placed(seed, kHome, kAway);
}

// ---------------------------------------------------------------------------
// Shot scenarios (docs/shot-decisions.md): home player 1 on the ball near the
// away goal, which away player 8 keeps -- the away side plays the reference
// tactic, whose first slot guards the goal -- and home plays without one.

// Away with the reference tactic: away player 8 keeps goal.
[[nodiscard]] std::expected<MatchSetup, std::string> againstKeeper(const std::uint64_t seed,
                                                                   const Side& home,
                                                                   const Side& away) {
  auto reference = SimTactics::Tactic::create(SimTactics::referenceTacticSpec());
  if (!reference) {
    return std::unexpected("invalid reference tactic: " + reference.error().front().message);
  }
  return placed(seed, home, away, {.home = std::nullopt, .away = *std::move(reference)});
}

// Home's teammates wait in their own half, out of sight; away's outfield
// players wait behind player 1, too far away to matter.
constexpr std::array<Placement, 6> kHomeBehind{{{.x = 20.0, .y = 8.0, .facingX = 1.0},
                                                {.x = 20.0, .y = 32.0, .facingX = 1.0},
                                                {.x = 15.0, .y = 20.0, .facingX = 1.0},
                                                {.x = 10.0, .y = 8.0, .facingX = 1.0},
                                                {.x = 10.0, .y = 32.0, .facingX = 1.0},
                                                {.x = 3.0, .y = 20.0, .facingX = 1.0}}};

// Player 1, eight meters out in front of the goal, has only the keeper to
// beat, standing on his line.
constexpr Side kClearChanceHome{{{.x = 51.5, .y = 20.0, .facingX = 1.0},
                                 kHomeBehind[0],
                                 kHomeBehind[1],
                                 kHomeBehind[2],
                                 kHomeBehind[3],
                                 kHomeBehind[4],
                                 kHomeBehind[5]}};
constexpr Side kClearChanceAway{{{.x = 59.5, .y = 20.0, .facingX = -1.0},
                                 {.x = 30.0, .y = 8.0, .facingX = -1.0},
                                 {.x = 30.0, .y = 32.0, .facingX = -1.0},
                                 {.x = 25.0, .y = 14.0, .facingX = -1.0},
                                 {.x = 25.0, .y = 26.0, .facingX = -1.0},
                                 {.x = 20.0, .y = 20.0, .facingX = -1.0},
                                 {.x = 35.0, .y = 20.0, .facingX = -1.0}}};

[[nodiscard]] std::expected<MatchSetup, std::string> clearChance(const std::uint64_t seed) {
  return againstKeeper(seed, kClearChanceHome, kClearChanceAway);
}

// The clear chance with a tactic on both sides and restarts on, to watch the
// kickoff after the goal: home plays the reference tactic with its slots in
// the order its players stand -- striker, wingers, holding midfielder, centre
// backs, goalkeeper -- so player 1 is its striker and player 7 its keeper.
[[nodiscard]] std::expected<MatchSetup, std::string> goalKickoff(const std::uint64_t seed) {
  SimTactics::TacticSpec spec = SimTactics::referenceTacticSpec();
  const std::vector<SimTactics::TacticSlot> slots = spec.slots;
  spec.name = "reference, striker first";
  spec.slots = {slots[6], slots[4], slots[5], slots[3], slots[1], slots[2], slots[0]};
  auto home = SimTactics::Tactic::create(std::move(spec));
  auto away = SimTactics::Tactic::create(SimTactics::referenceTacticSpec());
  if (!home || !away) {
    return std::unexpected("invalid goal-kickoff tactic");
  }
  auto setup = placed(seed, kClearChanceHome, kClearChanceAway,
                      {.home = *std::move(home), .away = *std::move(away)});
  if (setup) {
    setup->config.restarts.enabled = true;
  }
  return setup;
}

// Player 1 has run on to the goal line, six meters wide of the post, and looks
// infield: from there the goal is a sliver, and his teammate 2, free at the
// edge of the area, is the option.
[[nodiscard]] std::expected<MatchSetup, std::string> hopelessAngle(const std::uint64_t seed) {
  constexpr Side kHome{{{.x = 59.0, .y = 11.85, .facingX = 0.0, .facingY = 1.0},
                        {.x = 50.0, .y = 20.0, .facingX = 1.0},
                        kHomeBehind[1],
                        kHomeBehind[2],
                        kHomeBehind[3],
                        kHomeBehind[4],
                        kHomeBehind[5]}};
  constexpr Side kAway{{{.x = 59.5, .y = 18.0, .facingX = -1.0},
                        {.x = 30.0, .y = 8.0, .facingX = -1.0},
                        {.x = 30.0, .y = 32.0, .facingX = -1.0},
                        {.x = 25.0, .y = 14.0, .facingX = -1.0},
                        {.x = 25.0, .y = 26.0, .facingX = -1.0},
                        {.x = 20.0, .y = 20.0, .facingX = -1.0},
                        {.x = 35.0, .y = 20.0, .facingX = -1.0}}};
  return againstKeeper(seed, kHome, kAway);
}

// Player 1, twelve meters out in front of the goal, has away player 9 two
// meters in front of him, square in the way of every shot; his teammate 2
// stands free to his left.
[[nodiscard]] std::expected<MatchSetup, std::string> blockedLane(const std::uint64_t seed) {
  constexpr Side kHome{{{.x = 48.0, .y = 20.0, .facingX = 1.0},
                        {.x = 52.0, .y = 32.0, .facingX = 1.0},
                        kHomeBehind[1],
                        kHomeBehind[2],
                        kHomeBehind[3],
                        kHomeBehind[4],
                        kHomeBehind[5]}};
  constexpr Side kAway{{{.x = 59.5, .y = 20.0, .facingX = -1.0},
                        {.x = 50.5, .y = 20.0, .facingX = -1.0},
                        {.x = 30.0, .y = 8.0, .facingX = -1.0},
                        {.x = 25.0, .y = 14.0, .facingX = -1.0},
                        {.x = 25.0, .y = 26.0, .facingX = -1.0},
                        {.x = 20.0, .y = 20.0, .facingX = -1.0},
                        {.x = 35.0, .y = 20.0, .facingX = -1.0}}};
  return againstKeeper(seed, kHome, kAway);
}

// ---------------------------------------------------------------------------
// Goalkeeper scenarios (docs/goalkeeper.md): away keeps goal with the
// reference tactic, its keeper player 8 a perfect judge of a ball played in
// behind, so whether he comes is the model's call and not a misjudgement.

// The setup with away's keeper given a keeperAnticipation of 1.
[[nodiscard]] std::expected<MatchSetup, std::string> withSureKeeper(
    std::expected<MatchSetup, std::string> setup) {
  if (!setup) {
    return setup;
  }
  const MatchState& state = setup->initialState;
  std::vector<PlayerMatchState> players(state.players().begin(), state.players().end());
  players.at(kDefaultPlayersPerSide).attributes.keeperAnticipation = 1.0;
  auto sure = MatchState::create({.pitch = state.pitch(),
                                  .players = std::move(players),
                                  .ball = state.ball(),
                                  .playersPerSide = state.playersPerSide()},
                                 state.tactics());
  if (!sure) {
    return std::unexpected("invalid keeper scenario: " + sure.error().front().message);
  }
  setup->initialState = *std::move(sure);
  return setup;
}

// Home player 1 carries the ball across the pitch 27 m in front of away's
// goal, out of shooting range, and keeps it all the way. Away's outfield
// players neither press nor mark, so the keeper's position is all that moves
// with the ball.
[[nodiscard]] std::expected<MatchSetup, std::string> keeperArc(const std::uint64_t seed) {
  constexpr Side kHome{{{.x = 33.0, .y = 4.0, .facingX = 1.0},
                        kHomeBehind[0],
                        kHomeBehind[1],
                        kHomeBehind[2],
                        kHomeBehind[3],
                        kHomeBehind[4],
                        kHomeBehind[5]}};
  constexpr Side kAway{{{.x = 58.0, .y = 20.0, .facingX = -1.0},
                        {.x = 48.0, .y = 12.0, .facingX = -1.0},
                        {.x = 48.0, .y = 28.0, .facingX = -1.0},
                        {.x = 44.0, .y = 20.0, .facingX = -1.0},
                        {.x = 42.0, .y = 6.0, .facingX = -1.0},
                        {.x = 42.0, .y = 34.0, .facingX = -1.0},
                        {.x = 40.0, .y = 20.0, .facingX = -1.0}}};
  auto setup = againstKeeper(seed, kHome, kAway);
  if (setup) {
    setup->config.decisions.minHoldSeconds = 60.0;
    setup->config.defensive.pressRadius = 0.1;
    setup->config.defensive.markRadius = 0.1;
    setup->config.defensive.trackRadius = 0.1;
    setup->commands.push_back({.tick = SimCore::SimTick(0),
                               .command = MovePlayerCommand{.playerId = kFirstCarrier,
                                                            .target = {.x = 33.0, .y = 36.0}}});
  }
  return setup;
}

// Home player 1 plays a through ball from the halfway line into the space in
// front of away's penalty area, between away's centre backs. In the claim
// home's striker 2 starts wide and far from it, so the keeper is first to
// it; in the leave the striker is a few meters from where it goes.
[[nodiscard]] std::expected<MatchSetup, std::string> throughBall(const std::uint64_t seed,
                                                                 const Placement striker) {
  const Side home{{{.x = 34.0, .y = 20.0, .facingX = 1.0},
                   striker,
                   kHomeBehind[1],
                   kHomeBehind[2],
                   kHomeBehind[3],
                   kHomeBehind[4],
                   kHomeBehind[5]}};
  constexpr Side kAway{{{.x = 57.0, .y = 20.0, .facingX = -1.0},
                        {.x = 38.0, .y = 6.0, .facingX = -1.0},
                        {.x = 38.0, .y = 34.0, .facingX = -1.0},
                        {.x = 30.0, .y = 32.0, .facingX = -1.0},
                        {.x = 25.0, .y = 6.0, .facingX = -1.0},
                        {.x = 25.0, .y = 34.0, .facingX = -1.0},
                        {.x = 20.0, .y = 20.0, .facingX = -1.0}}};
  auto setup = withSureKeeper(againstKeeper(seed, home, kAway));
  if (setup) {
    const SimCore::Vec2 start = setup->initialState.ball().position;
    constexpr SimCore::Vec2 kTarget{.x = 48.0, .y = 20.0};
    setup->commands.push_back(
        {.tick = SimCore::SimTick(1),
         .command = PassCommand{.playerId = kFirstCarrier,
                                .target = kTarget,
                                .speed = planPassSpeed(SimCore::distance(start, kTarget),
                                                       setup->config.ball, setup->config.passing),
                                .receiver = SimCore::PlayerId(2)}});
  }
  return setup;
}

[[nodiscard]] std::expected<MatchSetup, std::string> keeperSweepClaim(const std::uint64_t seed) {
  return throughBall(seed, {.x = 36.0, .y = 36.0, .facingX = 1.0});
}

[[nodiscard]] std::expected<MatchSetup, std::string> keeperSweepLeave(const std::uint64_t seed) {
  return throughBall(seed, {.x = 45.0, .y = 20.0, .facingX = 1.0});
}

// ---------------------------------------------------------------------------
// Distribution scenarios (docs/goalkeeper-distribution.md): home plays the
// reference tactic in the order of its slots -- keeper, centre backs,
// holding midfielder, wingers, striker -- so player 1 is its keeper, on the
// ball in his own penalty area from the start; away plays without a tactic
// and stands where it is placed.

// The setup with home's keeper on the ball from the start, in his hands if
// `held`, and his tactic's directness set to `directness`.
[[nodiscard]] std::expected<MatchSetup, std::string> keeperOnTheBall(const std::uint64_t seed,
                                                                     const Side& home,
                                                                     const Side& away,
                                                                     const bool held,
                                                                     const double directness) {
  SimTactics::TacticSpec spec = SimTactics::referenceTacticSpec();
  if (directness != spec.principles.goalkeeper.directness) {
    spec.name = "reference, plays out";
    spec.principles.goalkeeper.directness = directness;
  }
  auto tactic = SimTactics::Tactic::create(std::move(spec));
  if (!tactic) {
    return std::unexpected("invalid distribution tactic: " + tactic.error().front().message);
  }
  auto setup = placed(seed, home, away, {.home = *std::move(tactic), .away = std::nullopt});
  if (!setup) {
    return setup;
  }
  const MatchState& state = setup->initialState;
  std::vector<PlayerMatchState> players(state.players().begin(), state.players().end());
  BallState ball = state.ball();
  ball.owner = kFirstCarrier;
  ball.held = held;
  auto owned = MatchState::create({.pitch = state.pitch(),
                                   .players = std::move(players),
                                   .ball = ball,
                                   .playersPerSide = state.playersPerSide()},
                                  state.tactics());
  if (!owned) {
    return std::unexpected("invalid distribution scenario: " + owned.error().front().message);
  }
  setup->initialState = *std::move(owned);
  setup->commands.clear();
  return setup;
}

// Home's keeper holds the ball and his tactic wants him to play out
// (directness 0.1). His centre backs stand wide and free; away marks his
// holding midfielder, wingers and striker a meter away, so every long ball
// would be a fifty-fifty.
[[nodiscard]] std::expected<MatchSetup, std::string> keeperBuildUp(const std::uint64_t seed) {
  constexpr Side kHome{{{.x = 3.0, .y = 20.0, .facingX = 1.0},
                        {.x = 12.0, .y = 10.0, .facingX = 1.0},
                        {.x = 12.0, .y = 30.0, .facingX = 1.0},
                        {.x = 20.0, .y = 20.0, .facingX = 1.0},
                        {.x = 30.0, .y = 5.0, .facingX = 1.0},
                        {.x = 30.0, .y = 35.0, .facingX = 1.0},
                        {.x = 38.0, .y = 20.0, .facingX = 1.0}}};
  constexpr Side kAway{{{.x = 59.0, .y = 20.0, .facingX = -1.0},
                        {.x = 21.0, .y = 20.5, .facingX = -1.0},
                        {.x = 31.0, .y = 5.5, .facingX = -1.0},
                        {.x = 31.0, .y = 34.5, .facingX = -1.0},
                        {.x = 39.0, .y = 20.5, .facingX = -1.0},
                        {.x = 48.0, .y = 12.0, .facingX = -1.0},
                        {.x = 48.0, .y = 28.0, .facingX = -1.0}}};
  return keeperOnTheBall(seed, kHome, kAway, true, 0.1);
}

// Home's keeper has the ball at his feet, his tactic neutral about going
// long (directness 0.5). Away presses high: a forward closes him down and
// three more stand in the lanes to his centre backs and his holding
// midfielder. Home's wingers and striker wait upfield, beyond the longest
// ground pass, away's last two defenders deep behind them.
[[nodiscard]] std::expected<MatchSetup, std::string> keeperLongKick(const std::uint64_t seed) {
  constexpr Side kHome{{{.x = 4.0, .y = 20.0, .facingX = 1.0},
                        {.x = 12.0, .y = 10.0, .facingX = 1.0},
                        {.x = 12.0, .y = 30.0, .facingX = 1.0},
                        {.x = 20.0, .y = 20.0, .facingX = 1.0},
                        {.x = 40.0, .y = 3.0, .facingX = 1.0},
                        {.x = 40.0, .y = 37.0, .facingX = 1.0},
                        {.x = 42.0, .y = 20.0, .facingX = 1.0}}};
  constexpr Side kAway{{{.x = 59.0, .y = 20.0, .facingX = -1.0},
                        {.x = 7.5, .y = 22.0, .facingX = -1.0},
                        {.x = 8.5, .y = 14.5, .facingX = -1.0},
                        {.x = 8.5, .y = 25.5, .facingX = -1.0},
                        {.x = 12.0, .y = 20.0, .facingX = -1.0},
                        {.x = 57.0, .y = 14.0, .facingX = -1.0},
                        {.x = 57.0, .y = 26.0, .facingX = -1.0}}};
  return keeperOnTheBall(seed, kHome, kAway, false, 0.5);
}

// Home player 1, wide of away's goal, plays the ball over its goal line:
// restarts are on, so away's keeper, player 8, takes the goal kick. Away
// plays the reference tactic; home's other players wait in their own half.
[[nodiscard]] std::expected<MatchSetup, std::string> goalKick(const std::uint64_t seed) {
  constexpr Side kHome{{{.x = 50.0, .y = 36.0, .facingX = 1.0},
                        kHomeBehind[0],
                        kHomeBehind[1],
                        kHomeBehind[2],
                        kHomeBehind[3],
                        kHomeBehind[4],
                        kHomeBehind[5]}};
  constexpr Side kAway{{{.x = 58.0, .y = 20.0, .facingX = -1.0},
                        {.x = 46.0, .y = 12.0, .facingX = -1.0},
                        {.x = 44.0, .y = 24.0, .facingX = -1.0},
                        {.x = 38.0, .y = 20.0, .facingX = -1.0},
                        {.x = 32.0, .y = 6.0, .facingX = -1.0},
                        {.x = 30.0, .y = 30.0, .facingX = -1.0},
                        {.x = 25.0, .y = 20.0, .facingX = -1.0}}};
  auto setup = againstKeeper(seed, kHome, kAway);
  if (setup) {
    setup->config.restarts.enabled = true;
    const SimCore::Vec2 start = setup->initialState.ball().position;
    constexpr SimCore::Vec2 kWide{.x = 62.0, .y = 38.0};
    setup->commands.push_back(
        {.tick = SimCore::SimTick(1),
         .command = PassCommand{.playerId = kFirstCarrier,
                                .target = kWide,
                                .speed = planPassSpeed(SimCore::distance(start, kWide),
                                                       setup->config.ball, setup->config.passing),
                                .receiver = std::nullopt}});
  }
  return setup;
}

constexpr std::array kScenarios{
    ScenarioDefinition{
        .name = "kickoff",
        .description = "seven-a-side kickoff fixture, the ball free on the center spot",
        .make = &kickoff},
    ScenarioDefinition{.name = "rolling-ball",
                       .description = "kickoff fixture with the ball rolling at (8, 3) m/s",
                       .make = &rollingBall},
    ScenarioDefinition{.name = "m0-acceptance",
                       .description = "all 14 players on scripted runs with target changes, "
                                      "rolling ball",
                       .make = &m0Acceptance},
    ScenarioDefinition{.name = "pass-chain",
                       .description = "home player 1 on the ball, teammates in a zigzag ahead, "
                                      "no opponent in reach",
                       .make = &passChain},
    ScenarioDefinition{.name = "intercepted-pass",
                       .description = "home player 1's only option is a risky pass past away "
                                      "player 8",
                       .make = &interceptedPass},
    ScenarioDefinition{.name = "no-passing-option",
                       .description = "home player 1 on the ball, every teammate behind him "
                                      "out of sight",
                       .make = &noPassingOption},
    ScenarioDefinition{.name = "clear-chance",
                       .description = "home player 1 eight meters out, only the keeper to beat",
                       .make = &clearChance},
    ScenarioDefinition{.name = "goal-kickoff",
                       .description = "the clear chance with tactics and restarts, the kickoff "
                                      "after the goal",
                       .make = &goalKickoff},
    ScenarioDefinition{.name = "hopeless-angle",
                       .description = "home player 1 on the goal line wide of the post, a "
                                      "teammate free",
                       .make = &hopelessAngle},
    ScenarioDefinition{.name = "blocked-lane",
                       .description = "home player 1 twelve meters out, a defender in the way, a "
                                      "teammate free",
                       .make = &blockedLane},
    ScenarioDefinition{.name = "keeper-arc",
                       .description = "home player 1 carries the ball across in front of away's "
                                      "goal, the keeper moves with it",
                       .make = &keeperArc},
    ScenarioDefinition{.name = "keeper-sweep-claim",
                       .description = "a through ball in behind away's line, the keeper first to "
                                      "it",
                       .make = &keeperSweepClaim},
    ScenarioDefinition{.name = "keeper-sweep-leave",
                       .description = "a through ball in behind away's line, home's striker "
                                      "first to it",
                       .make = &keeperSweepLeave},
    ScenarioDefinition{.name = "keeper-build-up",
                       .description = "home's keeper holds the ball and plays out to a free "
                                      "centre back",
                       .make = &keeperBuildUp},
    ScenarioDefinition{.name = "keeper-long-kick",
                       .description = "home's keeper on the ball under a high press goes long",
                       .make = &keeperLongKick},
    ScenarioDefinition{.name = "goal-kick",
                       .description = "home plays the ball over away's goal line, away's keeper "
                                      "takes the goal kick",
                       .make = &goalKick},
    ScenarioDefinition{.name = "tactic-match",
                       .description = "reference tactic against reference tactic, home kicks off",
                       .make = &tacticMatch},
    ScenarioDefinition{.name = "transition-3v2",
                       .description = "home wins the ball in midfield, three attackers against "
                                      "two defenders",
                       .make = &makeTransitionThreeVersusTwo},
    ScenarioDefinition{.name = "isolated-winger",
                       .description = "home plays out to an isolated winger, away presses on "
                                      "the trigger",
                       .make = &makeIsolatedWinger},
    ScenarioDefinition{.name = "touchline-trap",
                       .description = "away's receiver faces his own goal at the touchline, home "
                                      "presses four players",
                       .make = &touchlineTrap},
    ScenarioDefinition{.name = "lone-press",
                       .description = "the touchline trap with one home presser instead of four",
                       .make = &lonePress},
    ScenarioDefinition{.name = "run-behind-line",
                       .description = "home's striker level with away's defensive line, space "
                                      "behind it",
                       .make = &makeRunBehindTheLine},
};

}  // namespace

std::span<const ScenarioDefinition> scenarios() noexcept {
  return kScenarios;
}

std::expected<MatchSetup, std::string> makeTacticMatch(TeamTactics tactics,
                                                       const std::uint64_t seed) {
  auto fixture = makeSevenASideKickoff(Pitch(kPitchLength, kPitchWidth), {}, std::move(tactics));
  if (!fixture) {
    return std::unexpected("invalid tactic match: " + fixture.error().front().message);
  }
  // Home kicks off, from the same line-up as after a goal (docs/restarts.md).
  const MatchConfig config;
  const auto lineUp = lineUpForKickoff(*fixture, TeamSide::kHome, config.ball);
  if (!lineUp) {
    return std::unexpected("invalid tactic match: nobody to kick off");
  }
  std::vector<PlayerMatchState> players(fixture->players().begin(), fixture->players().end());
  for (std::size_t index = 0; index < players.size(); ++index) {
    players[index].position = lineUp->positions[index];
  }
  const SimCore::PlayerId taker = players[lineUp->takerIndex].playerId;
  auto state = MatchState::create({.pitch = fixture->pitch(),
                                   .players = std::move(players),
                                   .ball = fixture->ball(),
                                   .playersPerSide = fixture->playersPerSide()},
                                  fixture->tactics());
  if (!state) {
    return std::unexpected("invalid tactic match: " + state.error().front().message);
  }
  return MatchSetup{
      .initialState = *std::move(state),
      .config = config,
      .seed = seed,
      .commands = {{.tick = SimCore::SimTick(0), .command = GiveBallCommand{.playerId = taker}}}};
}

const ScenarioDefinition* findScenario(const std::string_view name) noexcept {
  for (const ScenarioDefinition& scenario : kScenarios) {
    if (scenario.name == name) {
      return &scenario;
    }
  }
  return nullptr;
}

}  // namespace ElyverseFootball::SimMatch
