#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <cstddef>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

#include "desiredRegion.hpp"
#include "kickoffScenario.hpp"
#include "matchCommand.hpp"
#include "matchSetup.hpp"
#include "matchSimulation.hpp"
#include "matchState.hpp"
#include "matchStateHash.hpp"
#include "referenceTactic.hpp"
#include "tactic.hpp"
#include "tacticalMovement.hpp"
#include "tacticalPhase.hpp"

using Catch::Matchers::WithinAbs;
using ElyverseFootball::SimCore::PlayerId;
using ElyverseFootball::SimCore::SimTick;
using ElyverseFootball::SimCore::Vec2;
using ElyverseFootball::SimMatch::chooseDesiredRegion;
using ElyverseFootball::SimMatch::DesiredRegion;
using ElyverseFootball::SimMatch::evaluatePosition;
using ElyverseFootball::SimMatch::GiveBallCommand;
using ElyverseFootball::SimMatch::hashMatchState;
using ElyverseFootball::SimMatch::makeSevenASideKickoff;
using ElyverseFootball::SimMatch::makeTacticalMovementSystem;
using ElyverseFootball::SimMatch::MatchConfig;
using ElyverseFootball::SimMatch::MatchSetup;
using ElyverseFootball::SimMatch::MatchSimulation;
using ElyverseFootball::SimMatch::MatchState;
using ElyverseFootball::SimMatch::MatchStateWriter;
using ElyverseFootball::SimMatch::MatchStepContext;
using ElyverseFootball::SimMatch::MatchSystem;
using ElyverseFootball::SimMatch::PerceptionConfig;
using ElyverseFootball::SimMatch::Pitch;
using ElyverseFootball::SimMatch::PositioningConfig;
using ElyverseFootball::SimMatch::ScheduledCommand;
using ElyverseFootball::SimMatch::startMatch;
using ElyverseFootball::SimMatch::tacticalTarget;
using ElyverseFootball::SimMatch::TeamSide;
using ElyverseFootball::SimMatch::TeamTactics;
using ElyverseFootball::SimTactics::referenceTacticSpec;
using ElyverseFootball::SimTactics::Tactic;
using ElyverseFootball::SimTactics::TacticalPhase;

namespace {

constexpr double kSecondsPerTick = 1.0 / 30.0;

[[nodiscard]] Tactic referenceTactic() {
  auto tactic = Tactic::create(referenceTacticSpec());
  REQUIRE(tactic.has_value());
  return *std::move(tactic);
}

[[nodiscard]] MatchState kickoff(const Vec2 ball = {.x = 30.0, .y = 20.0},
                                 TeamTactics tactics = {.home = referenceTactic(),
                                                        .away = referenceTactic()}) {
  auto state = makeSevenASideKickoff(Pitch(60.0, 40.0), {}, std::move(tactics));
  REQUIRE(state.has_value());
  if (ball == Vec2{.x = 30.0, .y = 20.0}) {
    return *std::move(state);
  }
  auto moved = MatchState::create(
      {.pitch = state->pitch(),
       .players = {state->players().begin(), state->players().end()},
       .ball = {.position = ball, .velocity = {}, .owner = std::nullopt, .lastTouch = std::nullopt},
       .playersPerSide = 7},
      state->tactics());
  REQUIRE(moved.has_value());
  return *std::move(moved);
}

// A match in which nobody passes: a forward -- home's by default -- keeps the
// ball from kickoff.
[[nodiscard]] MatchSimulation holdingMatch(MatchState state, const PlayerId carrier = PlayerId(7)) {
  MatchConfig config;
  config.decisions.minHoldSeconds = 1.0e6;
  return startMatch(MatchSetup{
      .initialState = std::move(state),
      .config = config,
      .seed = 4,
      .commands = {{.tick = SimTick(0), .command = GiveBallCommand{.playerId = carrier}}}});
}

void run(MatchSimulation& simulation, const SimTick::ValueType ticks) {
  while (simulation.tick() < SimTick(ticks)) {
    REQUIRE(simulation.step().has_value());
  }
}

[[nodiscard]] double distanceBetween(const Vec2 first, const Vec2 second) {
  return std::sqrt((first - second).lengthSquared());
}

// A system that sets one player's desired region once, at tick 0.
[[nodiscard]] MatchSystem placeRegion(const std::size_t index, const DesiredRegion region) {
  return {
      .name = "place region",
      .update = [index, region](const MatchStepContext&, const MatchState&,
                                MatchStateWriter& next) { next.tactical(index).region = region; },
      .intervalTicks = 1000,
      .phaseTicks = 0};
}

}  // namespace

TEST_CASE("The tactical target scales the slot into the phase's block", "[desiredRegion]") {
  const MatchState state = kickoff();
  // Progression, ball on the centre spot: the block's line at 20.1 m, 27 m
  // long, 34 m wide (see docs/desired-region.md for the arithmetic).
  const Vec2 centreBack = tacticalTarget(state, 1, TacticalPhase::kProgression);
  REQUIRE_THAT(centreBack.x, WithinAbs(20.1, 1e-9));
  REQUIRE_THAT(centreBack.y, WithinAbs(20.0 - ((0.18 / 0.76 - 0.5) * -34.0), 1e-9));
  // The goalkeeper stays near his goal.
  REQUIRE(tacticalTarget(state, 0, TacticalPhase::kProgression) == Vec2{.x = 2.4, .y = 20.0});
  // Away is the mirror image.
  const Vec2 awayCentreBack = tacticalTarget(state, 8, TacticalPhase::kProgression);
  REQUIRE_THAT(awayCentreBack.x, WithinAbs(60.0 - 20.1, 1e-9));
}

TEST_CASE("Responsibilities pull a target toward their lane and depth", "[desiredRegion]") {
  const MatchState state = kickoff();
  // The low-side winger provides width: onto the wing's centre line, 4 m in.
  const Vec2 winger = tacticalTarget(state, 4, TacticalPhase::kProgression);
  REQUIRE_THAT(winger.y, WithinAbs(4.0, 1e-9));
  REQUIRE_THAT(winger.x, WithinAbs(38.1, 1e-9));
  // The holding midfielder holds the rest defence, 5 to 20 m behind the
  // ball, with it; without it he keeps his place in the block.
  const Vec2 withBall = tacticalTarget(state, 3, TacticalPhase::kProgression);
  REQUIRE_THAT(withBall.x, WithinAbs(25.0, 1e-9));
  const Vec2 withoutBall = tacticalTarget(state, 3, TacticalPhase::kDefensiveBlock);
  REQUIRE(withoutBall.x > 0.0);
  REQUIRE(withoutBall != withBall);
}

TEST_CASE("The block shifts with the ball", "[desiredRegion]") {
  const MatchState centre = kickoff();
  const MatchState advanced = kickoff({.x = 50.0, .y = 32.0});
  for (std::size_t index = 1; index < 7; ++index) {
    CAPTURE(index);
    const Vec2 before = tacticalTarget(centre, index, TacticalPhase::kProgression);
    const Vec2 after = tacticalTarget(advanced, index, TacticalPhase::kProgression);
    REQUIRE(after.x > before.x);
    REQUIRE(after.y >= before.y);
  }
  // Without the ball the reference tactic's block is lower and narrower.
  const Vec2 attacking = tacticalTarget(centre, 6, TacticalPhase::kProgression);
  const Vec2 defending = tacticalTarget(centre, 6, TacticalPhase::kDefensiveBlock);
  REQUIRE(defending.x < attacking.x);
}

TEST_CASE("A position's cost has inspectable components", "[desiredRegion]") {
  // After a second the players remember each other and pitch control exists.
  MatchSimulation simulation = holdingMatch(kickoff());
  run(simulation, 30);
  const MatchState& state = simulation.state();
  const Vec2 target = tacticalTarget(state, 1, TacticalPhase::kProgression);
  const auto costAt = [&](const Vec2 candidate) {
    return evaluatePosition(state, 1, candidate, target, simulation.tick(), kSecondsPerTick,
                            PositioningConfig{}, PerceptionConfig{});
  };
  const auto atTarget = costAt(target);
  REQUIRE(atTarget.targetDistance == 0.0);
  REQUIRE(costAt(target + Vec2{.x = 5.0, .y = 0.0}).targetDistance == 0.5);
  // Right next to a teammate he remembers: crowded.
  const Vec2 teammate = state.players()[2].position;
  REQUIRE(costAt(teammate + Vec2{.x = 0.5, .y = 0.0}).spacing > 0.5);
  // Deep in the opponent's half: pressure and occupancy.
  const auto deep = costAt({.x = 55.0, .y = 20.0});
  REQUIRE(deep.occupancy > 0.9);
  REQUIRE(atTarget.occupancy < 0.5);
  // With the ball, a centre back ahead of it risks a counterattack.
  REQUIRE(costAt({.x = 45.0, .y = 20.0}).transitionRisk > 0.5);
  REQUIRE(atTarget.transitionRisk == 0.0);
  // The reference tactic's weights make the total.
  const auto sample = costAt({.x = 26.0, .y = 14.0});
  REQUIRE_THAT(sample.total,
               WithinAbs(sample.targetDistance + sample.spacing + (0.5 * sample.pressure) +
                             (0.5 * sample.occupancy) + (0.5 * sample.transitionRisk),
                         1e-12));
}

TEST_CASE("Hysteresis keeps a region that is almost as good", "[desiredRegion]") {
  const MatchState initial = kickoff();
  const Vec2 target = tacticalTarget(initial, 3, TacticalPhase::kDefensiveBlock);
  // One meter off the target costs 0.1 more: less than the hysteresis.
  const DesiredRegion previous{
      .tacticalTarget = target, .center = target + Vec2{.x = 0.0, .y = 1.0}, .cost = {}};
  MatchSimulation simulation({.initialState = initial,
                              .seed = 1,
                              .ticksPerSecond = 30,
                              .systems = {placeRegion(3, previous)},
                              .commands = {}});
  REQUIRE(simulation.step().has_value());

  const auto choose = [&](const PositioningConfig& config) {
    return chooseDesiredRegion(simulation.state(), 3, TacticalPhase::kDefensiveBlock,
                               simulation.tick(), kSecondsPerTick, config, PerceptionConfig{});
  };
  REQUIRE(choose(PositioningConfig{}).center == previous.center);
  PositioningConfig eager;
  eager.hysteresisCost = 0.0;
  REQUIRE(choose(eager).center == target);
}

TEST_CASE("Smoothing moves a region at most maxShiftMeters at a time", "[desiredRegion]") {
  const MatchState initial = kickoff();
  const Vec2 target = tacticalTarget(initial, 3, TacticalPhase::kDefensiveBlock);
  const DesiredRegion previous{
      .tacticalTarget = target, .center = target + Vec2{.x = -10.0, .y = 0.0}, .cost = {}};
  MatchSimulation simulation({.initialState = initial,
                              .seed = 1,
                              .ticksPerSecond = 30,
                              .systems = {placeRegion(3, previous)},
                              .commands = {}});
  REQUIRE(simulation.step().has_value());
  const DesiredRegion region =
      chooseDesiredRegion(simulation.state(), 3, TacticalPhase::kDefensiveBlock, simulation.tick(),
                          kSecondsPerTick, PositioningConfig{}, PerceptionConfig{});
  REQUIRE_THAT(distanceBetween(region.center, previous.center), WithinAbs(3.0, 1e-9));
  REQUIRE(region.center.x > previous.center.x);
  REQUIRE(region.tacticalTarget == target);
}

TEST_CASE("Players of a side with a tactic take up their regions", "[desiredRegion]") {
  // Without the ball, players hold their regions: away's forward (14) keeps
  // it, and away is scripted.
  MatchSimulation simulation = holdingMatch(
      kickoff({.x = 30.0, .y = 20.0}, {.home = referenceTactic(), .away = {}}), PlayerId(14));
  run(simulation, 300);
  const MatchState& state = simulation.state();
  for (std::size_t index = 0; index < 7; ++index) {
    CAPTURE(index);
    const auto& region = state.tactical(index).region;
    REQUIRE(region.has_value());
    REQUIRE_FALSE(state.tactical(index).action.has_value());
    const DesiredRegion& settled = region.value_or(DesiredRegion{});
    REQUIRE(state.players()[index].target == settled.center);
    REQUIRE(distanceBetween(state.players()[index].position, settled.center) < 1.5);
  }
  // The scripted away side has no regions and nowhere to go.
  for (std::size_t index = 7; index < 13; ++index) {
    REQUIRE_FALSE(state.tactical(index).region.has_value());
    REQUIRE_FALSE(state.players()[index].target.has_value());
  }
}

TEST_CASE("Settled players do not oscillate between regions", "[desiredRegion]") {
  // A ball held on the centre spot: the situation does not change, so once
  // settled no region may flip back to one it left.
  MatchSimulation simulation = holdingMatch(kickoff());
  run(simulation, 150);
  std::vector<std::vector<Vec2>> centres(14);
  while (simulation.tick() < SimTick(450)) {
    REQUIRE(simulation.step().has_value());
    for (std::size_t index = 0; index < 14; ++index) {
      if (const auto& region = simulation.state().tactical(index).region) {
        auto& history = centres.at(index);
        if (history.empty() || history.back() != region->center) {
          history.push_back(region->center);
        }
      }
    }
  }
  for (std::size_t index = 0; index < 14; ++index) {
    CAPTURE(index);
    const auto& history = centres.at(index);
    for (std::size_t later = 2; later < history.size(); ++later) {
      REQUIRE(history.at(later) != history.at(later - 2));
    }
  }
}

TEST_CASE("Tactical movement is deterministic", "[desiredRegion]") {
  MatchSimulation first = holdingMatch(kickoff());
  MatchSimulation second = holdingMatch(kickoff());
  run(first, 200);
  run(second, 200);
  REQUIRE(hashMatchState(first.state()) == hashMatchState(second.state()));
}

TEST_CASE("Tactical movement rejects an invalid configuration", "[desiredRegion]") {
  const auto withPositioning = [](const PositioningConfig& positioning) {
    return makeTacticalMovementSystem(
        {.positioning = positioning, .offBall = {}, .perception = {}});
  };
  PositioningConfig config;
  config.intervalTicks = 0;
  REQUIRE_THROWS_AS(withPositioning(config), std::invalid_argument);
  config = {};
  config.minConfidence = 1.5;
  REQUIRE_THROWS_AS(withPositioning(config), std::invalid_argument);
  config = {};
  config.maxShiftMeters = 0.0;
  REQUIRE_THROWS_AS(withPositioning(config), std::invalid_argument);
  REQUIRE_NOTHROW(withPositioning({}));
}
