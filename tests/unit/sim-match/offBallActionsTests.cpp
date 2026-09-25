#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

#include "actionCandidate.hpp"
#include "matchCommand.hpp"
#include "matchEvents.hpp"
#include "matchSetup.hpp"
#include "matchSimulation.hpp"
#include "matchState.hpp"
#include "offBallActions.hpp"
#include "perception.hpp"
#include "referenceTactic.hpp"
#include "tactic.hpp"
#include "zones.hpp"

using Catch::Matchers::WithinAbs;
using ElyverseFootball::SimCore::PlayerId;
using ElyverseFootball::SimCore::SimTick;
using ElyverseFootball::SimCore::Vec2;
using ElyverseFootball::SimMatch::ActionCandidate;
using ElyverseFootball::SimMatch::ActionDiagnostic;
using ElyverseFootball::SimMatch::ActionType;
using ElyverseFootball::SimMatch::GiveBallCommand;
using ElyverseFootball::SimMatch::isOffBallDecisionDue;
using ElyverseFootball::SimMatch::Lane;
using ElyverseFootball::SimMatch::laneOf;
using ElyverseFootball::SimMatch::laneOpenness;
using ElyverseFootball::SimMatch::MatchConfig;
using ElyverseFootball::SimMatch::MatchSetup;
using ElyverseFootball::SimMatch::MatchSimulation;
using ElyverseFootball::SimMatch::MatchState;
using ElyverseFootball::SimMatch::OffBallConfig;
using ElyverseFootball::SimMatch::Pitch;
using ElyverseFootball::SimMatch::PlayerMatchState;
using ElyverseFootball::SimMatch::RememberedPlayer;
using ElyverseFootball::SimMatch::ScheduledCommand;
using ElyverseFootball::SimMatch::startMatch;
using ElyverseFootball::SimMatch::TeamSide;
using ElyverseFootball::SimMatch::TeamTactics;
using ElyverseFootball::SimTactics::referenceTacticSpec;
using ElyverseFootball::SimTactics::Tactic;
using ElyverseFootball::SimTactics::TacticSpec;

namespace {

[[nodiscard]] Tactic tacticOf(TacticSpec spec) {
  auto tactic = Tactic::create(std::move(spec));
  REQUIRE(tactic.has_value());
  return *std::move(tactic);
}

[[nodiscard]] RememberedPlayer opponentAt(const Vec2 position) {
  return {.index = 0,
          .playerId = PlayerId(99),
          .position = position,
          .velocity = {},
          .confidence = 1.0,
          .teammate = false};
}

// Home plays the reference tactic, attacking +x; away is scripted. Home's
// striker (id 7) has the ball at (35, 20), pressed by away player 8 standing
// four meters behind him -- right in the lane to the holding midfielder (id
// 4), 10 m behind. The rest of away waits deep in its half.
[[nodiscard]] MatchState pressedCarrier(TeamTactics tactics) {
  const std::vector<std::pair<TeamSide, Vec2>> placements{
      {TeamSide::kHome, {.x = 3.0, .y = 20.0}},  {TeamSide::kHome, {.x = 15.0, .y = 12.0}},
      {TeamSide::kHome, {.x = 15.0, .y = 28.0}}, {TeamSide::kHome, {.x = 25.0, .y = 20.0}},
      {TeamSide::kHome, {.x = 40.0, .y = 4.0}},  {TeamSide::kHome, {.x = 40.0, .y = 36.0}},
      {TeamSide::kHome, {.x = 35.0, .y = 20.0}}, {TeamSide::kAway, {.x = 31.0, .y = 20.0}},
      {TeamSide::kAway, {.x = 57.0, .y = 20.0}}, {TeamSide::kAway, {.x = 52.0, .y = 10.0}},
      {TeamSide::kAway, {.x = 52.0, .y = 30.0}}, {TeamSide::kAway, {.x = 55.0, .y = 5.0}},
      {TeamSide::kAway, {.x = 55.0, .y = 35.0}}, {TeamSide::kAway, {.x = 50.0, .y = 20.0}}};
  std::vector<PlayerMatchState> players;
  players.reserve(placements.size());
  PlayerId::ValueType nextId = 1;
  for (const auto& [side, position] : placements) {
    players.push_back({.playerId = PlayerId(nextId++),
                       .side = side,
                       .position = position,
                       .velocity = {},
                       .attributes = {},
                       .target = std::nullopt,
                       .facing = {.x = side == TeamSide::kHome ? 1.0 : -1.0, .y = 0.0}});
  }
  auto state = MatchState::create({.pitch = Pitch(60.0, 40.0),
                                   .players = std::move(players),
                                   .ball = {.position = {.x = 35.5, .y = 20.0},
                                            .velocity = {},
                                            .owner = std::nullopt,
                                            .lastTouch = std::nullopt},
                                   .playersPerSide = 7},
                                  std::move(tactics));
  REQUIRE(state.has_value());
  return *std::move(state);
}

// The carrier keeps the ball: nobody passes.
[[nodiscard]] MatchSimulation holding(MatchState state, const std::uint64_t seed) {
  MatchConfig config;
  config.decisions.minHoldSeconds = 1.0e6;
  MatchSimulation simulation = startMatch(MatchSetup{
      .initialState = std::move(state),
      .config = config,
      .seed = seed,
      .commands = {{.tick = SimTick(0), .command = GiveBallCommand{.playerId = PlayerId(7)}}}});
  simulation.setCollectDiagnostics(true);
  return simulation;
}

// The first off-ball decision of a player in a run of `ticks`, which the
// test requires to happen.
[[nodiscard]] ActionDiagnostic firstDecision(MatchSimulation& simulation, const PlayerId player,
                                             const SimTick::ValueType ticks) {
  while (simulation.tick() < SimTick(ticks)) {
    REQUIRE(simulation.step().has_value());
    for (const ActionDiagnostic& diagnostic : simulation.actionDiagnostics()) {
      if (diagnostic.player == player) {
        return diagnostic;
      }
    }
  }
  FAIL("player " << player.value() << " made no off-ball decision");
  return {};
}

[[nodiscard]] double distanceToSegment(const Vec2 point, const Vec2 start, const Vec2 end) {
  const Vec2 segment = end - start;
  const double along = std::clamp((point - start).dot(segment) / segment.lengthSquared(), 0.0, 1.0);
  const Vec2 closest = start + (segment * along);
  return std::sqrt((point - closest).lengthSquared());
}

}  // namespace

TEST_CASE("A lane is as open as its nearest opponent is far", "[offBall]") {
  const Vec2 from{.x = 0.0, .y = 0.0};
  const Vec2 until{.x = 10.0, .y = 0.0};
  REQUIRE(laneOpenness(from, until, {}, 4.0) == 1.0);
  REQUIRE(laneOpenness(from, until, {opponentAt({.x = 5.0, .y = 0.0})}, 4.0) == 0.0);
  REQUIRE(laneOpenness(from, until, {opponentAt({.x = 5.0, .y = 2.0})}, 4.0) == 0.5);
  // Beyond the end the distance is to the end point.
  REQUIRE_THAT(laneOpenness(from, until, {opponentAt({.x = 13.0, .y = 0.0})}, 4.0),
               WithinAbs(0.75, 1e-12));
  RememberedPlayer teammate = opponentAt({.x = 5.0, .y = 0.0});
  teammate.teammate = true;
  REQUIRE(laneOpenness(from, until, {teammate}, 4.0) == 1.0);
}

TEST_CASE("Teammates of the carrier weigh every off-ball option", "[offBall]") {
  const Tactic reference = tacticOf(referenceTacticSpec());
  MatchSimulation simulation = holding(pressedCarrier({.home = reference, .away = {}}), 1);
  const ActionDiagnostic decision = firstDecision(simulation, PlayerId(5), 60);
  const std::vector<ActionCandidate>& candidates = decision.candidates;
  std::vector<ActionType> types;
  types.reserve(candidates.size());
  for (const ActionCandidate& candidate : candidates) {
    types.push_back(candidate.type);
    REQUIRE(candidate.utility == candidate.scores.total());
    REQUIRE(candidate.scores.region <= 0.0);
    REQUIRE(candidate.scores.effort <= 0.0);
  }
  REQUIRE(types == std::vector{ActionType::kHoldPosition, ActionType::kSupportCarrier,
                               ActionType::kMoveIntoSpace, ActionType::kRunInBehind,
                               ActionType::kCreateWidth, ActionType::kOccupyHalfspace});
  // The low-side winger (slot 4) provides width with weight 1 and runs in
  // behind with 0.5; he supports nobody.
  const OffBallConfig config;
  REQUIRE(candidates.at(4).scores.responsibility == config.responsibilityWeight * 1.0);
  REQUIRE(candidates.at(3).scores.responsibility == config.responsibilityWeight * 0.5);
  REQUIRE(candidates.at(1).scores.responsibility == 0.0);
  REQUIRE(candidates.at(1).subject == PlayerId(7));
  REQUIRE(candidates.at(0).scores.responsibility == config.holdResponsibility);
  // Width: on the wing's centre line, where he stands along the pitch.
  REQUIRE_THAT(candidates.at(4).target.y, WithinAbs(4.0, 1e-9));
  REQUIRE(decision.chosen.has_value());
}

TEST_CASE("Players near the ball decide more often", "[offBall]") {
  const Tactic reference = tacticOf(referenceTacticSpec());
  MatchSimulation simulation = holding(pressedCarrier({.home = reference, .away = {}}), 1);
  std::vector<SimTick> midfielder;  // 10 m from the ball
  std::vector<SimTick> keeper;      // 32 m from it
  while (simulation.tick() < SimTick(120)) {
    REQUIRE(simulation.step().has_value());
    for (const ActionDiagnostic& diagnostic : simulation.actionDiagnostics()) {
      if (diagnostic.player == PlayerId(4)) {
        midfielder.push_back(diagnostic.tick);
      } else if (diagnostic.player == PlayerId(1)) {
        keeper.push_back(diagnostic.tick);
      }
    }
  }
  // Every 6 ticks near the ball, every 18 far from it.
  REQUIRE(midfielder.size() >= 10);
  REQUIRE(keeper.size() >= 3);
  for (std::size_t index = 1; index < midfielder.size(); ++index) {
    REQUIRE(midfielder.at(index).value() - midfielder.at(index - 1).value() == 6);
  }
  for (std::size_t index = 1; index < keeper.size(); ++index) {
    REQUIRE(keeper.at(index).value() - keeper.at(index - 1).value() == 18);
  }
  const MatchState& state = simulation.state();
  REQUIRE_FALSE(isOffBallDecisionDue(state, 0, simulation.tick(), OffBallConfig{}));
}

TEST_CASE("A supporter opens a passing lane when the carrier is pressed", "[offBall]") {
  // The holding midfielder stands right behind the presser. Across seeds he
  // almost always picks a position the presser does not screen.
  const Tactic reference = tacticOf(referenceTacticSpec());
  const Vec2 carrier{.x = 35.0, .y = 20.0};
  const Vec2 presser{.x = 31.0, .y = 20.0};
  REQUIRE(distanceToSegment(presser, carrier, {.x = 25.0, .y = 20.0}) == 0.0);
  int opened = 0;
  constexpr int kSeeds = 20;
  for (std::uint64_t seed = 1; seed <= kSeeds; ++seed) {
    MatchSimulation simulation = holding(pressedCarrier({.home = reference, .away = {}}), seed);
    const ActionDiagnostic decision = firstDecision(simulation, PlayerId(4), 60);
    const ActionCandidate& chosen = decision.candidates.at(decision.chosen.value_or(0));
    if (distanceToSegment(presser, carrier, chosen.target) >= 3.0) {
      ++opened;
    }
  }

  REQUIRE(opened >= 18);
}

TEST_CASE("Width is kept when the tactic requires it", "[offBall]") {
  // The low-side winger over ten seconds of possession: with provideWidth he
  // stays on the wing; the same slot without it, in a narrow block, does not.
  auto narrowSpec = referenceTacticSpec();
  narrowSpec.name = "narrow";
  narrowSpec.slots.at(4).responsibilities.clear();
  for (auto& instruction : narrowSpec.phases) {
    instruction.blockWidth = 0.4;
  }
  const auto wingShare = [](const Tactic& tactic) {
    MatchSimulation simulation = holding(pressedCarrier({.home = tactic, .away = {}}), 3);
    int onWing = 0;
    int ticks = 0;
    while (simulation.tick() < SimTick(330)) {
      REQUIRE(simulation.step().has_value());
      if (simulation.tick() < SimTick(60)) {
        continue;
      }
      const PlayerMatchState& winger = simulation.state().players()[4];
      onWing +=
          laneOf(TeamSide::kHome, winger.position, simulation.state().pitch()) == Lane::kRightWing
              ? 1
              : 0;
      ++ticks;
    }
    return static_cast<double>(onWing) / static_cast<double>(ticks);
  };
  REQUIRE(wingShare(tacticOf(referenceTacticSpec())) >= 0.9);
  REQUIRE(wingShare(tacticOf(narrowSpec)) <= 0.5);
}

TEST_CASE("Off-ball actions end when the team loses the ball", "[offBall]") {
  const Tactic reference = tacticOf(referenceTacticSpec());
  MatchConfig config;
  config.decisions.minHoldSeconds = 1.0e6;
  MatchSimulation simulation = startMatch(MatchSetup{
      .initialState = pressedCarrier({.home = reference, .away = {}}),
      .config = config,
      .seed = 2,
      .commands = {{.tick = SimTick(0), .command = GiveBallCommand{.playerId = PlayerId(7)}},
                   {.tick = SimTick(60), .command = GiveBallCommand{.playerId = PlayerId(8)}}}});
  while (simulation.tick() < SimTick(59)) {
    REQUIRE(simulation.step().has_value());
  }
  REQUIRE(simulation.state().tactical(3).action.has_value());
  while (simulation.tick() < SimTick(90)) {
    REQUIRE(simulation.step().has_value());
  }
  for (std::size_t index = 0; index < 7; ++index) {
    REQUIRE_FALSE(simulation.state().tactical(index).action.has_value());
  }
}
