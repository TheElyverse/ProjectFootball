#include "matchSetup.hpp"

#include "playerMovement.hpp"

namespace ElyverseFootball::SimMatch {

std::vector<MatchSystem> makeMatchSystems(const MatchConfig& config) {
  std::vector<MatchSystem> systems;
  systems.push_back(makePerceptionSystem(config.perception));
  systems.push_back(makePursuitSystem(config.ball, config.pursuit));
  systems.push_back(makePassDecisionSystem(config.decisions, {.scoring = config.decisions.scoring,
                                                              .ball = config.ball,
                                                              .passing = config.passing,
                                                              .perception = config.perception,
                                                              .reception = config.reception}));
  systems.push_back(makePlayerMovementSystem());
  systems.push_back(makeBallMovementSystem(config.ball, config.passing, config.reception));
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
