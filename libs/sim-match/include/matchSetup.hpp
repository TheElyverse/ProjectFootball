#pragma once

#include <cstdint>
#include <vector>

#include "ballMovement.hpp"
#include "challenge.hpp"
#include "desiredRegion.hpp"
#include "matchCommand.hpp"
#include "matchSimulation.hpp"
#include "matchState.hpp"
#include "offBallActions.hpp"
#include "passDecision.hpp"
#include "passing.hpp"
#include "perception.hpp"
#include "pitchControl.hpp"
#include "pursuit.hpp"
#include "reception.hpp"
#include "restart.hpp"
#include "tacticalMovement.hpp"
#include "tacticalPhases.hpp"
#include "teamPressing.hpp"

namespace ElyverseFootball::SimMatch {

// The tunable parameters of the standard match systems. Everything here
// changes what a match does, so a replay records all of it; a parameter added
// here must be added to the replay format as well.
struct MatchConfig {
  int ticksPerSecond = kDefaultTicksPerSecond;
  BallPhysics ball;
  PerceptionConfig perception;
  PassConfig passing;
  ReceptionConfig reception;
  PursuitConfig pursuit;
  DecisionConfig decisions;
  PhaseConfig phases;
  PitchControlConfig pitchControl;
  PositioningConfig positioning;
  OffBallConfig offBall;
  DefensiveConfig defensive;
  ChallengeConfig challenge;
  PressingConfig pressing;
  RestartConfig restarts;

  friend bool operator==(const MatchConfig&, const MatchConfig&) = default;
};

// Everything a match with the standard systems starts from: the replay
// contract's InitialSnapshot, ordered commands and seed, plus the
// configuration. The core version completes the contract; it is not part of
// the setup because a build can only run its own.
struct MatchSetup {
  MatchState initialState;
  MatchConfig config;
  std::uint64_t seed = 0;
  std::vector<ScheduledCommand> commands;

  friend bool operator==(const MatchSetup&, const MatchSetup&) = default;
};

// The standard systems in their fixed update order. Changing the order or the
// set changes every match, like changing a system's behavior does.
//
//   1. perception       every perception.intervalTicks ticks
//   2. tactical phase   every phases.intervalTicks ticks
//   3. pitch control    every pitchControl.intervalTicks ticks
//   4. team pressing    every pressing.intervalTicks ticks
//   5. tactical movement every positioning.intervalTicks ticks
//   6. ball pursuit     every pursuit.intervalTicks ticks
//   7. pass decision    every decisions.intervalTicks ticks
//   8. ball challenge   every challenge.intervalTicks ticks
//   9. player movement  every tick
//  10. ball movement    every tick
//  11. restart          every tick, if restarts.enabled
//
// Tactical movement runs before pursuit, so in a step where both write the
// same player's target -- a new chaser -- pursuit's wins.
//
// Throws std::invalid_argument for invalid parameters.
[[nodiscard]] std::vector<MatchSystem> makeMatchSystems(const MatchConfig& config);

// A simulation of the setup with the standard systems. Throws
// std::invalid_argument for an invalid configuration or command.
[[nodiscard]] MatchSimulation startMatch(const MatchSetup& setup);

}  // namespace ElyverseFootball::SimMatch
