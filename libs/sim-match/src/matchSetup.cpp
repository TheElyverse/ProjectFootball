#include "matchSetup.hpp"

#include "playerMovement.hpp"

namespace ElyverseFootball::SimMatch {

std::vector<MatchSystem> makeMatchSystems(const MatchConfig& config) {
  std::vector<MatchSystem> systems;
  systems.push_back(makePerceptionSystem(config.perception));
  systems.push_back(makePhaseSystem(config.phases));
  systems.push_back(makePitchControlSystem(config.pitchControl));
  systems.push_back(
      makePressingSystem(config.pressing, config.perception, config.defensive.coverDistance));
  systems.push_back(makeTacticalMovementSystem({.positioning = config.positioning,
                                                .offBall = config.offBall,
                                                .defensive = config.defensive,
                                                .perception = config.perception}));
  systems.push_back(makePursuitSystem(config.ball, config.pursuit));
  systems.push_back(makePassDecisionSystem(config.decisions, {.scoring = config.decisions.scoring,
                                                              .ball = config.ball,
                                                              .passing = config.passing,
                                                              .perception = config.perception,
                                                              .reception = config.reception}));
  systems.push_back(makeChallengeSystem(config.challenge));
  systems.push_back(makePlayerMovementSystem());
  systems.push_back(
      makeBallMovementSystem(config.ball, config.passing, config.reception, config.restarts));
  // Only when enabled, so matches without restarts keep their system list.
  if (config.restarts.enabled) {
    systems.push_back(makeRestartSystem(config.restarts, config.ball));
  }
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
