#include "matchSetup.hpp"

#include "playerMovement.hpp"

namespace ElyverseFootball::SimMatch {

std::vector<MatchSystem> makeMatchSystems(const MatchConfig& config) {
  std::vector<MatchSystem> systems;
  systems.push_back(makePerceptionSystem(config.perception));
  systems.push_back(makePhaseSystem(config.phases));
  systems.push_back(makePitchControlSystem(config.pitchControl));
  systems.push_back(makePressingSystem({.pressing = config.pressing,
                                        .perception = config.perception,
                                        .coverDistance = config.defensive.coverDistance}));
  systems.push_back(
      makeTacticalMovementSystem({.positioning = config.positioning,
                                  .offBall = config.offBall,
                                  .defensive = config.defensive,
                                  .perception = config.perception,
                                  .goalkeeper = config.goalkeeper,
                                  .shotRange = config.decisions.shooting.maxShotDistance,
                                  .offside = config.offside}));
  // Only with restarts, which award them.
  if (config.restarts.enabled) {
    systems.push_back(makeSetPieceSystem(
        {.setPieces = config.setPieces, .physics = config.ball, .passing = config.passing}));
  }
  systems.push_back(makePursuitSystem({.physics = config.ball,
                                       .pursuit = config.pursuit,
                                       .reception = config.reception,
                                       .goalkeeper = config.goalkeeper}));
  systems.push_back(makePassDecisionSystem(config.decisions, {.scoring = config.decisions.scoring,
                                                              .ball = config.ball,
                                                              .passing = config.passing,
                                                              .perception = config.perception,
                                                              .reception = config.reception,
                                                              .offside = config.offside}));
  systems.push_back(makeDuelSystem({.duels = config.duels, .physics = config.ball}));
  systems.push_back(makeShotStoppingSystem({.saves = config.shotStopping, .physics = config.ball}));
  systems.push_back(
      makePlayerMovementSystem({.saves = config.shotStopping, .physics = config.ball}));
  systems.push_back(makeBallMovementSystem({.physics = config.ball,
                                            .passing = config.passing,
                                            .reception = config.reception,
                                            .restarts = config.restarts,
                                            .shooting = config.shooting,
                                            .woodwork = config.woodwork,
                                            .saves = config.shotStopping,
                                            .aerial = config.aerial,
                                            .duels = config.duels}));
  // Only when enabled, so matches without them keep their system list.
  if (config.offside.enabled) {
    systems.push_back(makeOffsideSystem(
        {.offside = config.offside, .physics = config.ball, .clock = config.clock}));
  }
  if (config.fouls.enabled) {
    systems.push_back(
        makeFoulSystem({.fouls = config.fouls, .physics = config.ball, .clock = config.clock}));
  }
  if (config.restarts.enabled) {
    systems.push_back(makeRestartSystem(
        {.restarts = config.restarts, .physics = config.ball, .clock = config.clock}));
  }
  systems.push_back(makeMatchClockSystem({.clock = config.clock,
                                          .fitness = config.fitness,
                                          .physics = config.ball,
                                          .ticksPerSecond = config.ticksPerSecond,
                                          .setPieces = config.restarts.enabled}));
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
