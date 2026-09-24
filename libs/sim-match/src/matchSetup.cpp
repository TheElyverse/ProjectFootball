#include "matchSetup.hpp"

#include "playerMovement.hpp"

namespace ElyverseFootball::SimMatch {

std::vector<MatchSystem> makeMatchSystems(const MatchConfig& config) {
  std::vector<MatchSystem> systems;
  systems.push_back(makePerceptionSystem(config.perception));
  systems.push_back(makePlayerMovementSystem());
  systems.push_back(makeBallMovementSystem(config.ball, config.passing));
  return systems;
}

MatchSimulation startMatch(const MatchSetup& setup) {
  return MatchSimulation({.initialState = setup.initialState,
                          .seed = setup.seed,
                          .ticksPerSecond = setup.config.ticksPerSecond,
                          .systems = makeMatchSystems(setup.config),
                          .commands = setup.commands});
}

}  // namespace ElyverseFootball::SimMatch
