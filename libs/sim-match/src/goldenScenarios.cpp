#include "goldenScenarios.hpp"

#include <array>
#include <cstddef>
#include <optional>
#include <utility>
#include <vector>

#include "ballMovement.hpp"
#include "matchCommand.hpp"
#include "matchState.hpp"
#include "passing.hpp"
#include "referenceTactic.hpp"
#include "responsibility.hpp"
#include "simTime.hpp"
#include "vec2.hpp"

namespace ElyverseFootball::SimMatch {
namespace {

// The sandbox pitch of every scenario.
constexpr double kLength = 60.0;
constexpr double kWidth = 40.0;

// Where a player stands and which way he faces: +1 toward the away goal
// (+x), -1 toward the home goal.
struct Spot {
  double x;
  double y;
  double facingX;
};

// Seven a side in tactic slot order: goalkeeper, two centre backs, holding
// midfielder, two wingers, striker (referenceTacticSpec()). Home is ids 1 to
// 7, away 8 to 14.
using Side = std::array<Spot, kDefaultPlayersPerSide>;

[[nodiscard]] SimTactics::Tactic referenceTactic() {
  auto tactic = SimTactics::Tactic::create(SimTactics::referenceTacticSpec());
  // The reference tactic is valid by construction; tests hold that.
  return *std::move(tactic);  // NOLINT(bugprone-unchecked-optional-access)
}

// The state with everyone placed and at rest, the ball at `ball`, and the
// commands to run.
[[nodiscard]] std::expected<MatchSetup, std::string> placed(
    const std::uint64_t seed, const Side& home, const Side& away, TeamTactics tactics,
    const SimCore::Vec2 ball, std::vector<ScheduledCommand> commands) {
  std::vector<PlayerMatchState> players;
  SimCore::PlayerId::ValueType nextId = 1;
  for (const auto& [side, spots] :
       {std::pair{TeamSide::kHome, &home}, std::pair{TeamSide::kAway, &away}}) {
    for (const Spot& spot : *spots) {
      players.push_back({.playerId = SimCore::PlayerId(nextId++),
                         .side = side,
                         .position = {.x = spot.x, .y = spot.y},
                         .velocity = {},
                         .attributes = {},
                         .target = std::nullopt,
                         .facing = {.x = spot.facingX, .y = 0.0}});
    }
  }
  auto state = MatchState::create(
      {.pitch = Pitch(kLength, kWidth),
       .players = std::move(players),
       .ball = {.position = ball, .velocity = {}, .owner = std::nullopt, .lastTouch = std::nullopt},
       .playersPerSide = kDefaultPlayersPerSide},
      std::move(tactics));
  if (!state) {
    return std::unexpected("invalid golden scenario: " + state.error().front().message);
  }
  return MatchSetup{.initialState = *std::move(state),
                    .config = {},
                    .seed = seed,
                    .commands = std::move(commands)};
}

[[nodiscard]] ScheduledCommand give(const std::int64_t tick,
                                    const SimCore::PlayerId::ValueType player) {
  return {.tick = SimCore::SimTick(tick),
          .command = GiveBallCommand{.playerId = SimCore::PlayerId(player)}};
}

// A pass command at the speed a pass of this length is planned with.
[[nodiscard]] ScheduledCommand pass(const std::int64_t tick,
                                    const SimCore::PlayerId::ValueType from,
                                    const SimCore::Vec2 origin, const SimCore::Vec2 target,
                                    const SimCore::PlayerId::ValueType receiver) {
  const double speed =
      planPassSpeed(SimCore::distance(origin, target), BallPhysics{}, PassConfig{});
  return {.tick = SimCore::SimTick(tick),
          .command = PassCommand{.playerId = SimCore::PlayerId(from),
                                 .target = target,
                                 .speed = speed,
                                 .receiver = SimCore::PlayerId(receiver)}};
}

}  // namespace

SimTactics::Tactic goldenPressingTactic(const double pressingIntensity,
                                        const std::vector<SimTactics::PressingTrigger>& triggers,
                                        const double pressingLine) {
  auto spec = SimTactics::referenceTacticSpec();
  spec.name = "golden-press";
  spec.principles.pressingLine = pressingLine;
  spec.principles.pressingTriggers = triggers;
  for (auto& phase : spec.phases) {
    phase.pressingIntensity = pressingIntensity;
  }
  auto tactic = SimTactics::Tactic::create(spec);
  return *std::move(tactic);  // NOLINT(bugprone-unchecked-optional-access)
}

std::expected<MatchSetup, std::string> makeTransitionThreeVersusTwo(const std::uint64_t seed) {
  // Home's holding midfielder (4) wins the ball from away's (11) at the
  // halfway line; wingers 5 and 6 and striker 7 against away's centre backs
  // 9 and 10. Away's wingers and striker are caught in home's half.
  constexpr Side kHome{{{.x = 4.0, .y = 20.0, .facingX = 1.0},
                        {.x = 18.0, .y = 13.0, .facingX = 1.0},
                        {.x = 18.0, .y = 27.0, .facingX = 1.0},
                        {.x = 30.0, .y = 20.0, .facingX = 1.0},
                        {.x = 36.0, .y = 8.0, .facingX = 1.0},
                        {.x = 36.0, .y = 32.0, .facingX = 1.0},
                        {.x = 38.0, .y = 20.0, .facingX = 1.0}}};
  constexpr Side kAway{{{.x = 57.0, .y = 20.0, .facingX = -1.0},
                        {.x = 46.0, .y = 16.0, .facingX = -1.0},
                        {.x = 46.0, .y = 24.0, .facingX = -1.0},
                        {.x = 31.0, .y = 21.0, .facingX = -1.0},
                        {.x = 22.0, .y = 8.0, .facingX = -1.0},
                        {.x = 22.0, .y = 32.0, .facingX = -1.0},
                        {.x = 20.0, .y = 20.0, .facingX = -1.0}}};
  return placed(seed, kHome, kAway, {.home = referenceTactic(), .away = referenceTactic()},
                {.x = 30.5, .y = 20.0}, {give(0, 11), give(10, 4)});
}

std::expected<MatchSetup, std::string> makeIsolatedWinger(const std::uint64_t seed) {
  // Home's goalkeeper 1 plays a long ball out to winger 6 on the touchline;
  // no home teammate is within 10 m of him, away's midfield is. Home is
  // scripted, so its players stay where they are and the winger stays
  // isolated.
  constexpr Side kHome{{{.x = 4.0, .y = 26.0, .facingX = 1.0},
                        {.x = 14.0, .y = 12.0, .facingX = 1.0},
                        {.x = 14.0, .y = 24.0, .facingX = 1.0},
                        {.x = 22.0, .y = 12.0, .facingX = 1.0},
                        {.x = 34.0, .y = 6.0, .facingX = 1.0},
                        {.x = 29.0, .y = 38.5, .facingX = 1.0},
                        {.x = 40.0, .y = 20.0, .facingX = 1.0}}};
  constexpr Side kAway{{{.x = 57.0, .y = 20.0, .facingX = -1.0},
                        {.x = 46.0, .y = 14.0, .facingX = -1.0},
                        {.x = 46.0, .y = 26.0, .facingX = -1.0},
                        {.x = 36.0, .y = 20.0, .facingX = -1.0},
                        {.x = 32.0, .y = 8.0, .facingX = -1.0},
                        {.x = 36.0, .y = 32.0, .facingX = -1.0},
                        {.x = 26.0, .y = 20.0, .facingX = -1.0}}};
  constexpr SimCore::Vec2 kFrom{.x = 4.5, .y = 26.0};
  return placed(
      seed, kHome, kAway,
      {.home = std::nullopt,
       .away = goldenPressingTactic(1.0, {SimTactics::PressingTrigger::kIsolatedReceiver}, 1.0)},
      kFrom, {give(0, 1), pass(1, 1, kFrom, {.x = 29.0, .y = 38.5}, 6)});
}

std::expected<MatchSetup, std::string> makePressingTrap(const std::uint64_t seed,
                                                        const TrapSpot spot,
                                                        const double pressingIntensity) {
  // Away's centre back 10 passes to its holding midfielder 11, who stands
  // with his back to his attacking direction -- facing away's own goal at
  // x = 60 -- at the touchline or in the centre. Home's players stand around
  // him; its presses decide what happens next.
  const double receiverY = spot == TrapSpot::kTouchline ? 37.5 : 20.0;
  const Side home{{{.x = 4.0, .y = 20.0, .facingX = 1.0},
                   {.x = 20.0, .y = 14.0, .facingX = 1.0},
                   {.x = 20.0, .y = 26.0, .facingX = 1.0},
                   {.x = 30.0, .y = receiverY == 20.0 ? 26.0 : 30.0, .facingX = 1.0},
                   {.x = 32.0, .y = 8.0, .facingX = 1.0},
                   {.x = 30.0, .y = receiverY == 20.0 ? 14.0 : 36.0, .facingX = 1.0},
                   {.x = 40.0, .y = 20.0, .facingX = 1.0}}};
  const Side away{{{.x = 57.0, .y = 20.0, .facingX = -1.0},
                   {.x = 48.0, .y = 12.0, .facingX = -1.0},
                   {.x = 48.0, .y = 28.0, .facingX = -1.0},
                   {.x = 36.0, .y = receiverY, .facingX = 1.0},
                   {.x = 30.0, .y = 6.0, .facingX = -1.0},
                   {.x = 26.0, .y = 30.0, .facingX = -1.0},
                   {.x = 24.0, .y = 20.0, .facingX = -1.0}}};
  const SimCore::Vec2 from{.x = 47.5, .y = 28.0};
  return placed(seed, home, away,
                {.home = goldenPressingTactic(
                     pressingIntensity, {SimTactics::PressingTrigger::kReceiverFacingOwnGoal}, 0.3),
                 .away = referenceTactic()},
                from, {give(0, 10), pass(1, 10, from, {.x = 36.0, .y = receiverY}, 11)});
}

std::expected<MatchSetup, std::string> makeRunBehindTheLine(const std::uint64_t seed) {
  // Home's holding midfielder 4 on the ball at the halfway line; striker 7
  // level with away's centre backs 9 and 10, 20 m of space behind them.
  constexpr Side kHome{{{.x = 4.0, .y = 20.0, .facingX = 1.0},
                        {.x = 18.0, .y = 13.0, .facingX = 1.0},
                        {.x = 18.0, .y = 27.0, .facingX = 1.0},
                        {.x = 28.0, .y = 20.0, .facingX = 1.0},
                        {.x = 34.0, .y = 6.0, .facingX = 1.0},
                        {.x = 34.0, .y = 34.0, .facingX = 1.0},
                        {.x = 40.0, .y = 20.0, .facingX = 1.0}}};
  constexpr Side kAway{{{.x = 58.0, .y = 20.0, .facingX = -1.0},
                        {.x = 40.0, .y = 13.0, .facingX = -1.0},
                        {.x = 40.0, .y = 27.0, .facingX = -1.0},
                        {.x = 34.0, .y = 20.0, .facingX = -1.0},
                        {.x = 30.0, .y = 8.0, .facingX = -1.0},
                        {.x = 30.0, .y = 32.0, .facingX = -1.0},
                        {.x = 22.0, .y = 20.0, .facingX = -1.0}}};
  return placed(seed, kHome, kAway, {.home = referenceTactic(), .away = referenceTactic()},
                {.x = 28.5, .y = 20.0}, {give(0, 4)});
}

}  // namespace ElyverseFootball::SimMatch
