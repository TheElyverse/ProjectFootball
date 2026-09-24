#pragma once

#include <cstdint>
#include <vector>

#include "ballMovement.hpp"
#include "matchCommand.hpp"
#include "matchSimulation.hpp"
#include "matchState.hpp"
#include "passing.hpp"
#include "perception.hpp"
#include "reception.hpp"

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
//   2. player movement  every tick
//   3. ball movement    every tick
//
// Throws std::invalid_argument for invalid parameters.
[[nodiscard]] std::vector<MatchSystem> makeMatchSystems(const MatchConfig& config);

// A simulation of the setup with the standard systems. Throws
// std::invalid_argument for an invalid configuration or command.
[[nodiscard]] MatchSimulation startMatch(const MatchSetup& setup);

}  // namespace ElyverseFootball::SimMatch
