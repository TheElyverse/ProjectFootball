#include <catch2/catch_test_macros.hpp>
#include <optional>
#include <stdexcept>
#include <utility>
#include <variant>
#include <vector>

#include "kickoffScenario.hpp"
#include "matchCommand.hpp"
#include "matchEvents.hpp"
#include "matchSetup.hpp"
#include "matchSimulation.hpp"
#include "matchState.hpp"
#include "matchStateHash.hpp"
#include "referenceTactic.hpp"
#include "tactic.hpp"
#include "tacticalPhase.hpp"
#include "tacticalPhases.hpp"

using ElyverseFootball::SimCore::PlayerId;
using ElyverseFootball::SimCore::SimTick;

using ElyverseFootball::SimMatch::classifyPhase;
using ElyverseFootball::SimMatch::GiveBallCommand;
using ElyverseFootball::SimMatch::hashMatchState;
using ElyverseFootball::SimMatch::makePhaseSystem;
using ElyverseFootball::SimMatch::makeSevenASideKickoff;
using ElyverseFootball::SimMatch::MatchConfig;
using ElyverseFootball::SimMatch::MatchEvent;
using ElyverseFootball::SimMatch::MatchSetup;
using ElyverseFootball::SimMatch::MatchSimulation;
using ElyverseFootball::SimMatch::MatchState;
using ElyverseFootball::SimMatch::MatchStateWriter;
using ElyverseFootball::SimMatch::MatchStepContext;
using ElyverseFootball::SimMatch::MatchSystem;
using ElyverseFootball::SimMatch::PhaseChanged;
using ElyverseFootball::SimMatch::PhaseConfig;
using ElyverseFootball::SimMatch::PhaseSituation;
using ElyverseFootball::SimMatch::Pitch;
using ElyverseFootball::SimMatch::ScheduledCommand;
using ElyverseFootball::SimMatch::startMatch;
using ElyverseFootball::SimMatch::teamOnTheBall;
using ElyverseFootball::SimMatch::TeamPhase;
using ElyverseFootball::SimMatch::TeamPossession;
using ElyverseFootball::SimMatch::TeamSide;
using ElyverseFootball::SimMatch::TeamTactics;
using ElyverseFootball::SimMatch::updatePossession;
using ElyverseFootball::SimTactics::referenceTacticSpec;
using ElyverseFootball::SimTactics::Tactic;
using ElyverseFootball::SimTactics::TacticalPhase;

namespace {

constexpr double kLength = 60.0;

[[nodiscard]] Tactic referenceTactic() {
  auto tactic = Tactic::create(referenceTacticSpec());
  REQUIRE(tactic.has_value());
  return *std::move(tactic);
}

[[nodiscard]] MatchState kickoff(TeamTactics tactics = {.home = referenceTactic(),
                                                        .away = referenceTactic()}) {
  auto state = makeSevenASideKickoff(Pitch(kLength, 40.0), {}, std::move(tactics));
  REQUIRE(state.has_value());
  return *std::move(state);
}

// A situation for home, with the reference tactic's pressing line at 0.6.
struct Situation {
  Tactic tactic = referenceTactic();

  [[nodiscard]] TacticalPhase classify(const std::optional<TeamSide> team, const double ballDepth,
                                       const std::optional<TacticalPhase> previous = std::nullopt,
                                       const bool fromOpponent = false,
                                       const double seconds = 10.0) const {
    return classifyPhase(
        PhaseSituation{
            .side = TeamSide::kHome,
            .tactic = &tactic,
            .possession = {.team = team, .since = SimTick(0), .fromOpponent = fromOpponent},
            .ballDepth = ballDepth,
            .pitchLength = kLength,
            .possessionSeconds = seconds,
            .previous = previous},
        PhaseConfig{});
  }
};

// A match whose carriers never pass, so possession changes only through the
// commands of a test.
[[nodiscard]] MatchSimulation matchOf(MatchState state, std::vector<ScheduledCommand> commands) {
  MatchConfig config;
  config.decisions.minHoldSeconds = 1.0e6;
  return startMatch(MatchSetup{.initialState = std::move(state),
                               .config = config,
                               .seed = 3,
                               .commands = std::move(commands)});
}

[[nodiscard]] ScheduledCommand give(const SimTick::ValueType tick,
                                    const PlayerId::ValueType player) {
  return {.tick = SimTick(tick), .command = GiveBallCommand{.playerId = PlayerId(player)}};
}

// The phase changes of a run to `ticks`.
[[nodiscard]] std::vector<PhaseChanged> runPhases(MatchSimulation& simulation,
                                                  const SimTick::ValueType ticks) {
  std::vector<PhaseChanged> changes;
  while (simulation.tick() < SimTick(ticks)) {
    REQUIRE(simulation.step().has_value());
    for (const MatchEvent& event : simulation.events()) {
      if (const auto* changed = std::get_if<PhaseChanged>(&event)) {
        changes.push_back(*changed);
      }
    }
  }
  return changes;
}

}  // namespace

TEST_CASE("The ball belongs to its owner's team, or its last toucher's", "[tacticalPhases]") {
  MatchState state = kickoff();
  REQUIRE_FALSE(teamOnTheBall(state).has_value());

  MatchSimulation simulation = matchOf(state, {give(0, 9)});
  REQUIRE(simulation.step().has_value());
  REQUIRE(teamOnTheBall(simulation.state()) == TeamSide::kAway);
}

TEST_CASE("Possession records when and from whom a team won the ball", "[tacticalPhases]") {
  MatchSimulation simulation = matchOf(kickoff(), {give(0, 3), give(20, 12)});
  REQUIRE(simulation.step().has_value());
  const TeamPossession first = updatePossession(simulation.state(), {}, SimTick(1));
  REQUIRE(first ==
          TeamPossession{.team = TeamSide::kHome, .since = SimTick(0), .fromOpponent = false});
  // The same team keeps its possession, however often the ball moves on.
  REQUIRE(updatePossession(simulation.state(), first, SimTick(5)) == first);

  while (simulation.tick() < SimTick(21)) {
    REQUIRE(simulation.step().has_value());
  }
  REQUIRE(updatePossession(simulation.state(), first, SimTick(21)) ==
          TeamPossession{.team = TeamSide::kAway, .since = SimTick(20), .fromOpponent = true});
}

TEST_CASE("A team with the ball is in the phase of the third it is in", "[tacticalPhases]") {
  const Situation home;
  REQUIRE(home.classify(TeamSide::kHome, 5.0) == TacticalPhase::kBuildUp);
  REQUIRE(home.classify(TeamSide::kHome, 25.0) == TacticalPhase::kProgression);
  REQUIRE(home.classify(TeamSide::kHome, 45.0) == TacticalPhase::kFinalThird);
  REQUIRE(home.classify(TeamSide::kHome, 60.0) == TacticalPhase::kFinalThird);
}

TEST_CASE("A team without the ball presses beyond its pressing line", "[tacticalPhases]") {
  const Situation home;
  // Pressing line 0.6 of 60 m: 36 m from the own goal line.
  REQUIRE(home.classify(TeamSide::kAway, 30.0) == TacticalPhase::kDefensiveBlock);
  REQUIRE(home.classify(TeamSide::kAway, 36.0) == TacticalPhase::kPressing);
  // Before anyone had the ball, the same rule applies.
  REQUIRE(home.classify(std::nullopt, 50.0) == TacticalPhase::kPressing);
  REQUIRE(home.classify(std::nullopt, 10.0) == TacticalPhase::kDefensiveBlock);
}

TEST_CASE("Hysteresis keeps the phase while the ball sits near a boundary", "[tacticalPhases]") {
  const Situation home;
  using enum TacticalPhase;
  // Third boundaries at 20 m and 40 m, margin 3 m.
  REQUIRE(home.classify(TeamSide::kHome, 22.0, kBuildUp) == kBuildUp);
  REQUIRE(home.classify(TeamSide::kHome, 23.5, kBuildUp) == kProgression);
  REQUIRE(home.classify(TeamSide::kHome, 18.0, kProgression) == kProgression);
  REQUIRE(home.classify(TeamSide::kHome, 16.0, kProgression) == kBuildUp);
  REQUIRE(home.classify(TeamSide::kHome, 38.0, kFinalThird) == kFinalThird);
  // Two thirds at once is no boundary to hesitate at.
  REQUIRE(home.classify(TeamSide::kHome, 41.0, kBuildUp) == kFinalThird);
  // Coming out of a transition, the raw third applies.
  REQUIRE(home.classify(TeamSide::kHome, 22.0, kAttackingTransition) == kProgression);

  // Pressing line at 36 m.
  REQUIRE(home.classify(TeamSide::kAway, 34.0, kPressing) == kPressing);
  REQUIRE(home.classify(TeamSide::kAway, 32.5, kPressing) == kDefensiveBlock);
  REQUIRE(home.classify(TeamSide::kAway, 38.0, kDefensiveBlock) == kDefensiveBlock);
  REQUIRE(home.classify(TeamSide::kAway, 39.5, kDefensiveBlock) == kPressing);
}

TEST_CASE("A possession change starts a transition for both teams", "[tacticalPhases]") {
  const Situation home;
  using enum TacticalPhase;
  REQUIRE(home.classify(TeamSide::kHome, 25.0, kDefensiveBlock, true, 0.0) == kAttackingTransition);
  REQUIRE(home.classify(TeamSide::kHome, 25.0, kAttackingTransition, true, 3.9) ==
          kAttackingTransition);
  REQUIRE(home.classify(TeamSide::kHome, 25.0, kAttackingTransition, true, 4.0) == kProgression);
  REQUIRE(home.classify(TeamSide::kAway, 25.0, kProgression, true, 1.0) == kDefensiveTransition);
  REQUIRE(home.classify(TeamSide::kAway, 25.0, kDefensiveTransition, true, 4.0) == kDefensiveBlock);
  // Winning a ball nobody had, as at kickoff, is no transition.
  REQUIRE(home.classify(TeamSide::kHome, 25.0, std::nullopt, false, 0.0) == kProgression);
}

TEST_CASE("Phases follow a match: kickoff, turnover, transition, press", "[tacticalPhases]") {
  // Home's forward (player 7) kicks off; at tick 60 away's goalkeeper
  // (player 8) gets the ball deep in his own half.
  MatchSimulation simulation = matchOf(kickoff(), {give(0, 7), give(60, 8)});
  const std::vector<PhaseChanged> changes = runPhases(simulation, 300);

  using enum TacticalPhase;
  const std::vector<PhaseChanged> expected{
      // The ball on the centre spot: middle third for home, below away's
      // pressing line.
      {.tick = SimTick(0),
       .side = TeamSide::kHome,
       .previous = std::nullopt,
       .phase = kProgression},
      {.tick = SimTick(0),
       .side = TeamSide::kAway,
       .previous = std::nullopt,
       .phase = kDefensiveBlock},
      // The turnover: both teams in transition for four seconds.
      {.tick = SimTick(60),
       .side = TeamSide::kHome,
       .previous = kProgression,
       .phase = kDefensiveTransition},
      {.tick = SimTick(60),
       .side = TeamSide::kAway,
       .previous = kDefensiveBlock,
       .phase = kAttackingTransition},
      // Then away builds up from its goal, and home presses it there.
      {.tick = SimTick(180),
       .side = TeamSide::kHome,
       .previous = kDefensiveTransition,
       .phase = kPressing},
      {.tick = SimTick(180),
       .side = TeamSide::kAway,
       .previous = kAttackingTransition,
       .phase = kBuildUp},
  };
  REQUIRE(changes == expected);
  REQUIRE(simulation.state().phase(TeamSide::kHome) ==
          TeamPhase{.phase = kPressing, .since = SimTick(180)});
  REQUIRE(simulation.state().possession() ==
          TeamPossession{.team = TeamSide::kAway, .since = SimTick(60), .fromOpponent = true});
}

TEST_CASE("A scripted side has no phase", "[tacticalPhases]") {
  MatchSimulation simulation =
      matchOf(kickoff({.home = referenceTactic(), .away = std::nullopt}), {give(0, 7)});
  const std::vector<PhaseChanged> changes = runPhases(simulation, 30);
  REQUIRE(changes.size() == 1);
  REQUIRE(changes.front().side == TeamSide::kHome);
  REQUIRE_FALSE(simulation.state().phase(TeamSide::kAway).has_value());
  // Possession is tracked either way.
  REQUIRE(simulation.state().possession().team == TeamSide::kHome);

  MatchSimulation scripted = matchOf(kickoff({}), {give(0, 7)});
  REQUIRE(runPhases(scripted, 30).empty());
}

TEST_CASE("Only a side with a tactic can be given a phase", "[tacticalPhases]") {
  const MatchSystem setAwayPhase{
      .name = "set phase",
      .update = [](const MatchStepContext&, const MatchState&, MatchStateWriter& next) {
        next.setPhase(TeamSide::kAway,
                      TeamPhase{.phase = TacticalPhase::kPressing, .since = SimTick(0)});
      }};
  MatchSimulation simulation({.initialState = kickoff({.home = referenceTactic(), .away = {}}),
                              .seed = 1,
                              .ticksPerSecond = 30,
                              .systems = {setAwayPhase},
                              .commands = {}});
  REQUIRE_THROWS_AS((void)simulation.step(), std::invalid_argument);
}

TEST_CASE("Possession and phases are part of the state hash", "[tacticalPhases]") {
  const MatchState initial = kickoff();
  const auto hashAfter = [&initial](MatchSystem system) {
    MatchSimulation simulation({.initialState = initial,
                                .seed = 1,
                                .ticksPerSecond = 30,
                                .systems = {std::move(system)},
                                .commands = {}});
    REQUIRE(simulation.step().has_value());
    return hashMatchState(simulation.state());
  };
  const auto writing = [](TeamPossession possession, std::optional<TeamPhase> phase) {
    return MatchSystem{.name = "write",
                       .update = [possession, phase](const MatchStepContext&, const MatchState&,
                                                     MatchStateWriter& next) {
                         next.setPossession(possession);
                         next.setPhase(TeamSide::kHome, phase);
                       }};
  };
  const TeamPossession home{.team = TeamSide::kHome, .since = SimTick(0), .fromOpponent = false};
  const TeamPhase pressing{.phase = TacticalPhase::kPressing, .since = SimTick(0)};
  const auto base = hashAfter(writing(home, pressing));
  REQUIRE(base != hashMatchState(initial));
  REQUIRE(hashAfter(writing({.team = TeamSide::kAway, .since = SimTick(0), .fromOpponent = false},
                            pressing)) != base);
  REQUIRE(hashAfter(writing({.team = TeamSide::kHome, .since = SimTick(0), .fromOpponent = true},
                            pressing)) != base);
  REQUIRE(hashAfter(writing(
              home, TeamPhase{.phase = TacticalPhase::kBuildUp, .since = SimTick(0)})) != base);
  REQUIRE(hashAfter(writing(home, std::nullopt)) != base);
  REQUIRE(hashAfter(writing(home, pressing)) == base);
}

TEST_CASE("The phase system rejects an invalid configuration", "[tacticalPhases]") {
  REQUIRE_THROWS_AS(makePhaseSystem({.intervalTicks = 0}), std::invalid_argument);
  REQUIRE_THROWS_AS(makePhaseSystem({.intervalTicks = 10, .transitionSeconds = -1.0}),
                    std::invalid_argument);
  REQUIRE_THROWS_AS(
      makePhaseSystem({.intervalTicks = 10, .transitionSeconds = 4.0, .hysteresisMeters = -0.5}),
      std::invalid_argument);
  REQUIRE_NOTHROW(makePhaseSystem({}));
}
