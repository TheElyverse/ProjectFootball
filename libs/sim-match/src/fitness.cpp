#include "fitness.hpp"

#include <algorithm>
#include <stdexcept>

#include "configChecks.hpp"

namespace ElyverseFootball::SimMatch {

using ConfigChecks::isPositiveFinite;

void validate(const FitnessConfig& config) {
  // False for NaN.
  const bool valid = config.sprintSpeedShare > 0.0 && config.sprintSpeedShare <= 1.0 &&
                     isPositiveFinite(config.sprintSeconds) &&
                     isPositiveFinite(config.recoverySeconds) && config.fullMatchSpeedLoss >= 0.0 &&
                     config.fullMatchSpeedLoss < 1.0;
  if (!valid) {
    throw std::invalid_argument("fitness: invalid configuration");
  }
}

PlayerFitness stepFitness(const PlayerMatchState& player, const FitnessTime& time,
                          const FitnessConfig& config) noexcept {
  const PlayerFitness& fitness = player.fitness;
  PlayerFitness next = fitness;
  ++next.playedTicks;
  const bool sprinting =
      player.velocity.length() > config.sprintSpeedShare * player.attributes.maxSpeed;
  next.sprintReserve =
      std::clamp(fitness.sprintReserve + (sprinting ? -time.secondsPerTick / config.sprintSeconds
                                                    : time.secondsPerTick / config.recoverySeconds),
                 0.0, 1.0);
  // The share stops at the ticks of both halves, which the first half's added
  // time counts toward.
  const double played = std::min(
      1.0, static_cast<double>(next.playedTicks) / static_cast<double>(2 * time.halfTicks));
  next.topSpeedShare = 1.0 - (config.fullMatchSpeedLoss * played);
  if (next.sprintReserve <= 0.0) {
    next.topSpeedShare = std::min(next.topSpeedShare, config.sprintSpeedShare);
  }
  return next;
}

}  // namespace ElyverseFootball::SimMatch
