#include "scenarios.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <string>
#include <utility>
#include <vector>

#include "ballMovement.hpp"
#include "ids.hpp"
#include "kickoffScenario.hpp"
#include "matchCommand.hpp"
#include "matchSetup.hpp"
#include "matchState.hpp"
#include "pitch.hpp"
#include "vec2.hpp"

namespace ElyverseFootball::SimMatch {
namespace {

// The sandbox pitch: an example 7v7 size, not a mandated one.
constexpr double kPitchLength = 60.0;
constexpr double kPitchWidth = 40.0;

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

// A hand-placed player: where he stands and which way he faces.
struct Placement {
  double x;
  double y;
  // +1: facing the away goal (+x), -1: facing the home goal.
  double facingX;
};

using Side = std::array<Placement, kDefaultPlayersPerSide>;

constexpr SimCore::PlayerId kFirstCarrier{1};

// Home players get ids 1 to 7 in order, away players 8 to 14. The ball starts
// at the first carrier's feet and is given to him at tick 0.
[[nodiscard]] std::expected<MatchSetup, std::string> placed(const std::uint64_t seed,
                                                            const Side& home, const Side& away) {
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
                         .facing = {.x = placement.facingX, .y = 0.0}});
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
                                   .playersPerSide = kDefaultPlayersPerSide});
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
// carrier sees teammates ahead of him. The away side stands out of play along
// the home goal line, behind every pass.
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
};

}  // namespace

std::span<const ScenarioDefinition> scenarios() noexcept {
  return kScenarios;
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
