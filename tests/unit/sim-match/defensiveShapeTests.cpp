#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

#include "actionCandidate.hpp"
#include "defensiveActions.hpp"
#include "matchCommand.hpp"
#include "matchEvents.hpp"
#include "matchSetup.hpp"
#include "matchSimulation.hpp"
#include "matchState.hpp"
#include "referenceTactic.hpp"
#include "tactic.hpp"
#include "tacticalPhase.hpp"
#include "zones.hpp"

using ElyverseFootball::SimCore::PlayerId;
using ElyverseFootball::SimCore::SimTick;
using ElyverseFootball::SimCore::Vec2;
using ElyverseFootball::SimMatch::ActionCandidate;
using ElyverseFootball::SimMatch::ActionDiagnostic;
using ElyverseFootball::SimMatch::ActionType;
using ElyverseFootball::SimMatch::GiveBallCommand;
using ElyverseFootball::SimMatch::MatchConfig;
using ElyverseFootball::SimMatch::MatchSetup;
using ElyverseFootball::SimMatch::MatchSimulation;
using ElyverseFootball::SimMatch::MatchState;
using ElyverseFootball::SimMatch::measureTeamShape;
using ElyverseFootball::SimMatch::MovePlayerCommand;
using ElyverseFootball::SimMatch::Pitch;
using ElyverseFootball::SimMatch::PlayerMatchState;
using ElyverseFootball::SimMatch::ScheduledCommand;
using ElyverseFootball::SimMatch::startMatch;
using ElyverseFootball::SimMatch::TeamShape;
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

// Home plays a tactic and defends; away is scripted, its player 8 on the
// ball at `ball`, player 9 at `runner`. Home stands in a compact 1-2-1-3,
// everyone facing the away goal; the rest of away waits deep in its half.
[[nodiscard]] MatchState defending(const Tactic& home, const Vec2 ball,
                                   const Vec2 runner = {.x = 50.0, .y = 36.0}) {
  const std::vector<std::pair<TeamSide, Vec2>> placements{
      {TeamSide::kHome, {.x = 3.0, .y = 20.0}},
      {TeamSide::kHome, {.x = 15.0, .y = 12.0}},
      {TeamSide::kHome, {.x = 15.0, .y = 28.0}},
      {TeamSide::kHome, {.x = 22.0, .y = 20.0}},
      {TeamSide::kHome, {.x = 30.0, .y = 5.0}},
      {TeamSide::kHome, {.x = 30.0, .y = 35.0}},
      {TeamSide::kHome, {.x = 35.0, .y = 20.0}},
      {TeamSide::kAway, ball},
      {TeamSide::kAway, runner},
      {TeamSide::kAway, {.x = 57.0, .y = 20.0}},
      {TeamSide::kAway, {.x = 55.0, .y = 8.0}},
      {TeamSide::kAway, {.x = 55.0, .y = 32.0}},
      {TeamSide::kAway, {.x = 52.0, .y = 14.0}},
      {TeamSide::kAway, {.x = 52.0, .y = 26.0}}};
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
  auto state = MatchState::create(
      {.pitch = Pitch(60.0, 40.0),
       .players = std::move(players),
       .ball = {.position = ball, .velocity = {}, .owner = std::nullopt, .lastTouch = std::nullopt},
       .playersPerSide = 7},
      {.home = home, .away = std::nullopt});
  REQUIRE(state.has_value());
  return *std::move(state);
}

// Away's player 8 keeps the ball; extra commands script the rest.
[[nodiscard]] MatchSimulation match(MatchState state, std::vector<ScheduledCommand> commands = {},
                                    const std::uint64_t seed = 5) {
  MatchConfig config;
  config.decisions.minHoldSeconds = 1.0e6;
  commands.insert(commands.begin(),
                  {.tick = SimTick(0), .command = GiveBallCommand{.playerId = PlayerId(8)}});
  MatchSimulation simulation = startMatch(MatchSetup{.initialState = std::move(state),
                                                     .config = config,
                                                     .seed = seed,
                                                     .commands = std::move(commands)});
  simulation.setCollectDiagnostics(true);
  return simulation;
}

// Every action decision of a run to `ticks`.
[[nodiscard]] std::vector<ActionDiagnostic> run(MatchSimulation& simulation,
                                                const SimTick::ValueType ticks) {
  std::vector<ActionDiagnostic> decisions;
  while (simulation.tick() < SimTick(ticks)) {
    REQUIRE(simulation.step().has_value());
    decisions.insert(decisions.end(), simulation.actionDiagnostics().begin(),
                     simulation.actionDiagnostics().end());
  }
  return decisions;
}

[[nodiscard]] TeamShape homeShape(const MatchState& state) {
  const auto shape = measureTeamShape(state, TeamSide::kHome);
  REQUIRE(shape.has_value());
  return shape.value_or(TeamShape{});
}

// Whether any home player offered himself an action about this opponent.
[[nodiscard]] bool considered(const std::vector<ActionDiagnostic>& decisions, const ActionType type,
                              const PlayerId subject) {
  for (const ActionDiagnostic& decision : decisions) {
    for (const ActionCandidate& candidate : decision.candidates) {
      if (candidate.type == type && candidate.subject == subject) {
        return true;
      }
    }
  }
  return false;
}

}  // namespace

TEST_CASE("Defenders weigh holding, marking, tracking and covering", "[defensiveShape]") {
  // Away's player 9 stands between home's centre backs, in their zone.
  MatchSimulation simulation = match(
      defending(tacticOf(referenceTacticSpec()), {.x = 40.0, .y = 20.0}, {.x = 20.0, .y = 16.0}));
  const std::vector<ActionDiagnostic> decisions = run(simulation, 7);
  const ActionDiagnostic* centreBack = nullptr;
  for (const ActionDiagnostic& decision : decisions) {
    if (decision.player == PlayerId(2)) {
      centreBack = &decision;
    }
  }
  REQUIRE(centreBack != nullptr);
  const auto& candidates = centreBack->candidates;
  REQUIRE(candidates.front().type == ActionType::kHoldPosition);
  // Centre back: the line is his primary duty, marking and cover 0.6.
  REQUIRE(candidates.front().scores.responsibility == 1.0);
  const auto mark =
      std::ranges::find(candidates, ActionType::kMarkOpponent, &ActionCandidate::type);
  REQUIRE(mark != candidates.end());
  REQUIRE(mark->subject == PlayerId(9));
  REQUIRE(mark->scores.responsibility == 0.6);
  // Goal-side: between the forward and the home goal, 1.5 m from him.
  REQUIRE(mark->target.x < 20.0);
  REQUIRE(std::abs(std::sqrt((mark->target - Vec2{.x = 20.0, .y = 16.0}).lengthSquared()) - 1.5) <
          1e-9);
  const auto cover = std::ranges::find(candidates, ActionType::kCover, &ActionCandidate::type);
  REQUIRE(cover != candidates.end());
  REQUIRE(cover->subject.has_value());
}

TEST_CASE("The block shifts toward the ball side", "[defensiveShape]") {
  const Tactic reference = tacticOf(referenceTacticSpec());
  const auto centroidY = [&reference](const Vec2 ball) {
    MatchSimulation simulation = match(defending(reference, ball));
    (void)run(simulation, 150);
    return homeShape(simulation.state()).centroid.y;
  };
  const double left = centroidY({.x = 40.0, .y = 6.0});
  const double right = centroidY({.x = 40.0, .y = 34.0});
  REQUIRE(right - left > 6.0);
}

TEST_CASE("A high and a deep line hold measurably different heights", "[defensiveShape]") {
  const auto withLine = [](const double lineHeight) {
    auto spec = referenceTacticSpec();
    spec.name = "line";
    for (const auto phase : {ElyverseFootball::SimTactics::TacticalPhase::kDefensiveBlock,
                             ElyverseFootball::SimTactics::TacticalPhase::kPressing,
                             ElyverseFootball::SimTactics::TacticalPhase::kDefensiveTransition}) {
      spec.phases.at(ElyverseFootball::SimTactics::phaseIndex(phase)).lineHeight = lineHeight;
    }
    return tacticOf(spec);
  };
  const auto lineAfterFiveSeconds = [](const Tactic& tactic) {
    MatchSimulation simulation = match(defending(tactic, {.x = 42.0, .y = 20.0}));
    (void)run(simulation, 150);
    return homeShape(simulation.state()).defensiveLine;
  };
  const double deep = lineAfterFiveSeconds(withLine(0.12));
  const double high = lineAfterFiveSeconds(withLine(0.4));
  REQUIRE(high - deep > 8.0);
}

TEST_CASE("Only runners a defender has seen are tracked", "[defensiveShape]") {
  const Tactic reference = tacticOf(referenceTacticSpec());
  const ScheduledCommand run9{
      .tick = SimTick(0),
      .command = MovePlayerCommand{.playerId = PlayerId(9), .target = {.x = 2.0, .y = 24.0}}};
  SECTION("a run in front of the defence is seen and tracked") {
    MatchSimulation simulation =
        match(defending(reference, {.x = 40.0, .y = 20.0}, {.x = 28.0, .y = 26.0}), {run9});
    REQUIRE(considered(run(simulation, 45), ActionType::kTrackRunner, PlayerId(9)));
  }
  SECTION("a run behind every defender's back is not") {
    // Player 9 starts behind the whole outfield, which faces the other way.
    MatchSimulation simulation =
        match(defending(reference, {.x = 40.0, .y = 20.0}, {.x = 9.0, .y = 34.0}), {run9});
    REQUIRE_FALSE(considered(run(simulation, 45), ActionType::kTrackRunner, PlayerId(9)));
  }
}

TEST_CASE("Defensive decisions reject an invalid configuration", "[defensiveShape]") {
  using ElyverseFootball::SimMatch::DefensiveConfig;
  using ElyverseFootball::SimMatch::validate;
  DefensiveConfig config;
  config.markRadius = 0.0;
  REQUIRE_THROWS_AS(validate(config), std::invalid_argument);
  config = {};
  config.holdResponsibility = 2.0;
  REQUIRE_THROWS_AS(validate(config), std::invalid_argument);
  config = {};
  config.urgencyWeight = -1.0;
  REQUIRE_THROWS_AS(validate(config), std::invalid_argument);
  REQUIRE_NOTHROW(validate(DefensiveConfig{}));
}
