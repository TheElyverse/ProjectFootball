#include <catch2/catch_test_macros.hpp>
#include <set>
#include <string_view>

#include "matchSetup.hpp"
#include "matchState.hpp"
#include "scenarios.hpp"

using ElyverseFootball::SimMatch::checkStartingPositions;
using ElyverseFootball::SimMatch::findScenario;
using ElyverseFootball::SimMatch::ScenarioDefinition;
using ElyverseFootball::SimMatch::scenarios;
using ElyverseFootball::SimMatch::startMatch;

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
