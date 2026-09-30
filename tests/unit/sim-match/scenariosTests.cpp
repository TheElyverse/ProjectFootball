#include <array>
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <set>
#include <string>
#include <string_view>
#include <variant>

#include "matchEvents.hpp"
#include "matchSetup.hpp"
#include "matchState.hpp"
#include "restartKind.hpp"
#include "scenarios.hpp"
#include "tactic.hpp"
#include "tacticJson.hpp"

using ElyverseFootball::SimMatch::checkStartingPositions;
using ElyverseFootball::SimMatch::findScenario;
using ElyverseFootball::SimMatch::makeTacticMatch;
using ElyverseFootball::SimMatch::ScenarioDefinition;
using ElyverseFootball::SimMatch::scenarios;
using ElyverseFootball::SimMatch::startMatch;
using ElyverseFootball::SimMatch::TeamSide;
using ElyverseFootball::SimTactics::loadTactic;
using ElyverseFootball::SimTactics::Tactic;

TEST_CASE("Every scenario builds a valid, runnable setup", "[scenarios]") {
  REQUIRE_FALSE(scenarios().empty());
  std::set<std::string_view> names;
  for (const ScenarioDefinition& scenario : scenarios()) {
    CAPTURE(scenario.name);
    REQUIRE(names.insert(scenario.name).second);
    REQUIRE_FALSE(scenario.description.empty());

    const auto setup = scenario.make(99);
    REQUIRE(setup.has_value());
    REQUIRE(setup->seed == 99);
    REQUIRE(checkStartingPositions(setup->initialState).empty());
    auto simulation = startMatch(*setup);
    for (int tick = 0; tick < 30; ++tick) {
      REQUIRE(simulation.step().has_value());
    }
  }
}

TEST_CASE("A scenario is the same setup every time", "[scenarios]") {
  for (const ScenarioDefinition& scenario : scenarios()) {
    CAPTURE(scenario.name);
    REQUIRE(scenario.make(5) == scenario.make(5));
  }
}

TEST_CASE("Scenarios are found by name", "[scenarios]") {
  const ScenarioDefinition* kickoff = findScenario("kickoff");
  REQUIRE(kickoff != nullptr);
  REQUIRE(kickoff->name == "kickoff");
  REQUIRE(findScenario("no-such-scenario") == nullptr);
}

TEST_CASE("Any two tactic presets can face each other", "[scenarios]") {
  const std::filesystem::path directory = std::filesystem::path(PF_DATA_DIR) / "tactics";
  constexpr std::array<std::string_view, 4> kPresets{"reference", "possession", "counter",
                                                     "pressing"};
  for (const std::string_view homeName : kPresets) {
    for (const std::string_view awayName : kPresets) {
      CAPTURE(homeName, awayName);
      const auto home = loadTactic(directory / (std::string(homeName) + ".json"));
      const auto away = loadTactic(directory / (std::string(awayName) + ".json"));
      REQUIRE(home.has_value());
      REQUIRE(away.has_value());
      const auto setup = makeTacticMatch({.home = *home, .away = *away}, 3);
      REQUIRE(setup.has_value());
      REQUIRE(setup->initialState.tactics().of(TeamSide::kHome) == *home);
      REQUIRE(setup->initialState.tactics().of(TeamSide::kAway) == *away);
      REQUIRE(setup == makeTacticMatch({.home = *home, .away = *away}, 3));
      auto simulation = startMatch(*setup);
      for (int tick = 0; tick < 60; ++tick) {
        REQUIRE(simulation.step().has_value());
      }
    }
  }
}

TEST_CASE("The goal-kickoff scenario scores and kicks off from the line-up", "[scenarios]") {
  using ElyverseFootball::SimMatch::GoalScored;
  using ElyverseFootball::SimMatch::MatchEvent;
  using ElyverseFootball::SimMatch::RestartKind;
  using ElyverseFootball::SimMatch::RestartTaken;
  const ScenarioDefinition* scenario = findScenario("goal-kickoff");
  REQUIRE(scenario != nullptr);
  const auto setup = scenario->make(2);
  REQUIRE(setup.has_value());
  auto simulation = startMatch(*setup);
  bool scored = false;
  bool kickedOff = false;
  while (!kickedOff && simulation.tick().value() < 150) {
    REQUIRE(simulation.step().has_value());
    for (const MatchEvent& event : simulation.events()) {
      scored = scored || std::holds_alternative<GoalScored>(event);
      const auto* restart = std::get_if<RestartTaken>(&event);
      kickedOff = kickedOff || (restart != nullptr && restart->kind == RestartKind::kKickoff);
    }
  }
  REQUIRE(scored);
  REQUIRE(kickedOff);
  // Away, which conceded, kicks off from the centre spot; everybody stands
  // in his own half.
  const auto& state = simulation.state();
  REQUIRE(state.ball().position == state.pitch().center());
  REQUIRE(state.ball().owner == ElyverseFootball::SimCore::PlayerId(14));
  // Pitch control from before the kickoff is gone until its next refresh.
  REQUIRE_FALSE(state.pitchControl().has_value());
  for (const auto& player : state.players()) {
    CAPTURE(player.playerId.value());
    REQUIRE(
        (player.side == TeamSide::kHome ? player.position.x <= 30.0 : player.position.x >= 30.0));
  }
}
