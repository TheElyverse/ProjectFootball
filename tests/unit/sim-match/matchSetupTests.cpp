#include <catch2/catch_test_macros.hpp>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "ballMovement.hpp"
#include "kickoffScenario.hpp"
#include "matchCommand.hpp"
#include "matchSetup.hpp"
#include "matchSimulation.hpp"
#include "passDecision.hpp"
#include "perception.hpp"
#include "playerMovement.hpp"
#include "pursuit.hpp"

using ElyverseFootball::SimCore::PlayerId;
using ElyverseFootball::SimCore::SimTick;
using ElyverseFootball::SimMatch::kBallMovementSystemName;
using ElyverseFootball::SimMatch::kPassDecisionSystemName;
using ElyverseFootball::SimMatch::kPerceptionSystemName;
using ElyverseFootball::SimMatch::kPhaseSystemName;
using ElyverseFootball::SimMatch::kPitchControlSystemName;
using ElyverseFootball::SimMatch::kPlayerMovementSystemName;
using ElyverseFootball::SimMatch::kPursuitSystemName;
using ElyverseFootball::SimMatch::makeMatchSystems;
using ElyverseFootball::SimMatch::makeSevenASideKickoff;
using ElyverseFootball::SimMatch::MatchConfig;
using ElyverseFootball::SimMatch::MatchSetup;
using ElyverseFootball::SimMatch::MatchSimulation;
using ElyverseFootball::SimMatch::MatchSystem;
using ElyverseFootball::SimMatch::MovePlayerCommand;
using ElyverseFootball::SimMatch::Pitch;
using ElyverseFootball::SimMatch::startMatch;

namespace {

[[nodiscard]] MatchSetup kickoffSetup(MatchConfig config = {}) {
  auto state = makeSevenASideKickoff(Pitch(60.0, 40.0), {.x = 5.0, .y = 1.0});
  REQUIRE(state.has_value());
  return {.initialState = *std::move(state),
          .config = config,
          .seed = 11,
          .commands = {{.tick = SimTick(0),
                        .command = MovePlayerCommand{.playerId = PlayerId(4),
                                                     .target = {.x = 20.0, .y = 30.0}}}}};
}

}  // namespace

TEST_CASE("The standard systems run in their documented order", "[matchSetup]") {
  std::vector<std::string> names;
  for (const MatchSystem& system : makeMatchSystems({})) {
    names.push_back(system.name);
  }

  REQUIRE(names == std::vector<std::string>{
                       std::string(kPerceptionSystemName), std::string(kPhaseSystemName),
                       std::string(kPitchControlSystemName), std::string(kPursuitSystemName),
                       std::string(kPassDecisionSystemName), std::string(kPlayerMovementSystemName),
                       std::string(kBallMovementSystemName)});
}

TEST_CASE("startMatch runs the setup with the standard systems", "[matchSetup]") {
  MatchConfig config;
  config.ticksPerSecond = 60;
  const MatchSetup setup = kickoffSetup(config);
  MatchSimulation simulation = startMatch(setup);

  for (int tick = 0; tick < 60; ++tick) {
    REQUIRE(simulation.step().has_value());
  }

  REQUIRE(simulation.ticksPerSecond() == 60);
  REQUIRE(simulation.elapsedSeconds() == 1.0);
  REQUIRE(simulation.state().players()[3].target.has_value());
  REQUIRE_FALSE(simulation.state().players()[3].position ==
                setup.initialState.players()[3].position);
  REQUIRE_FALSE(simulation.state().ball().position == setup.initialState.ball().position);
}

TEST_CASE("startMatch rejects an invalid configuration", "[matchSetup]") {
  MatchConfig noTicks;
  noTicks.ticksPerSecond = 0;
  MatchConfig noFriction;
  noFriction.ball.rollingDeceleration = 0.0;
  MatchConfig noMemory;
  noMemory.perception.memorySeconds = 0.0;

  for (const MatchConfig& config : {noTicks, noFriction, noMemory}) {
    REQUIRE_THROWS_AS(startMatch(kickoffSetup(config)), std::invalid_argument);
  }
}
