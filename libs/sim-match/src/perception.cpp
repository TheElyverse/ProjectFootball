#include "perception.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <string>

namespace ElyverseFootball::SimMatch {
namespace {

using SimCore::SimTick;
using SimCore::Vec2;

[[nodiscard]] bool isFiniteNonNegative(const double value) noexcept {
  return value >= 0.0 && value <= std::numeric_limits<double>::max();
}

void validate(const PerceptionConfig& config) {
  const bool valid = config.intervalTicks >= 1 && isFiniteNonNegative(config.viewDistance) &&
                     config.fieldOfViewDegrees > 0.0 && config.fieldOfViewDegrees <= 360.0 &&
                     isFiniteNonNegative(config.awarenessRadius) &&
                     isFiniteNonNegative(config.memorySeconds) && config.memorySeconds > 0.0 &&
                     isFiniteNonNegative(config.extrapolationSeconds);
  if (!valid) {
    throw std::invalid_argument("perception: invalid configuration");
  }
}

[[nodiscard]] double secondsBetween(const SimTick earlier, const SimTick later,
                                    const double secondsPerTick) noexcept {
  return static_cast<double>(later.value() - earlier.value()) * secondsPerTick;
}

// cos(angle) for an angle in [0, pi], from basic arithmetic only, so the cone
// edge is the same bit pattern on every platform: std::cos, like std::exp
// (see SimCore::stableExp), need not be correctly rounded, and a last-bit
// difference would decide differently for an entity right on the edge.
//
// The Taylor series of cos(angle / 2) up to the 22nd power, in Horner form
// 1 - x²/(1·2)·(1 - x²/(3·4)·(1 - ...)), then cos(angle) = 2·cos²(angle / 2) - 1.
// Halving keeps x within pi / 2, where the first omitted term is below 1e-19;
// the result is within a few 1e-16 of the true cosine.
[[nodiscard]] double stableCosine(const double angle) noexcept {
  const double half = angle / 2.0;
  const double square = half * half;
  double sum = 1.0;
  for (int power = 22; power >= 2; power -= 2) {
    sum = 1.0 - ((square / static_cast<double>((power - 1) * power)) * sum);
  }
  return (2.0 * sum * sum) - 1.0;
}

// The cosine a position's direction must reach to lie in the vision cone:
// cos(fov / 2) less a tolerance. stableCosine(pi / 2) is about 2e-16, not 0,
// so without the tolerance a player straight sideways would fall outside a
// 180 degree cone despite the inclusive edge.
[[nodiscard]] double coneCosine(const PerceptionConfig& config) noexcept {
  constexpr double kEdgeTolerance = 1e-9;
  return stableCosine(config.fieldOfViewDegrees * std::numbers::pi / 360.0) - kEdgeTolerance;
}

// canSee() with the cone's cosine already computed, so perceive() computes it
// once per observer rather than once per entity.
[[nodiscard]] bool isVisible(const PlayerMatchState& observer, const Vec2 position,
                             const PerceptionConfig& config, const double minCosine) noexcept {
  const Vec2 offset = position - observer.position;
  const double distanceSquared = offset.lengthSquared();
  if (distanceSquared > config.viewDistance * config.viewDistance) {
    return false;
  }
  if (distanceSquared <= config.awarenessRadius * config.awarenessRadius) {
    return true;
  }
  // Inside the cone when the angle to the facing is at most half the field
  // of view: facing · offset >= |offset| · cos(fov / 2). The facing is a unit
  // vector, so no angle needs to be computed per entity.
  return observer.facing.dot(offset) >= std::sqrt(distanceSquared) * minCosine;
}

// A fresh observation of an entity seen right now.
[[nodiscard]] Observation seen(const ObservedEntity entity, const Vec2 position,
                               const Vec2 velocity, const SimTick now) noexcept {
  return {.entity = entity,
          .position = position,
          .velocity = velocity,
          .confidence = 1.0,
          .lastSeen = now};
}

}  // namespace

bool canSee(const PlayerMatchState& observer, const Vec2 position,
            const PerceptionConfig& config) noexcept {
  return isVisible(observer, position, config, coneCosine(config));
}

Vec2 estimatePosition(const Observation& observation, const SimTick now,
                      const double secondsPerTick, const PerceptionConfig& config) noexcept {
  const double age = secondsBetween(observation.lastSeen, now, secondsPerTick);
  return observation.position + (observation.velocity * std::min(age, config.extrapolationSeconds));
}

void perceive(const MatchState& state, const std::size_t observerIndex, const SimTick now,
              const double secondsPerTick, const PerceptionConfig& config,
              PlayerPerception& memory) {
  const PlayerMatchState& observer = state.players()[observerIndex];
  const PlayerPerception& previous = state.perception(observerIndex);

  // Build into memory's own storage: clear() keeps its capacity, so a
  // steady-state update allocates nothing.
  std::vector<Observation>& observations = memory.observations;
  observations.clear();
  const double minCosine = coneCosine(config);
  const auto observe = [&](const ObservedEntity entity, const Vec2 position, const Vec2 velocity) {
    if (isVisible(observer, position, config, minCosine)) {
      observations.push_back(seen(entity, position, velocity, now));
      return;
    }
    const Observation* remembered = previous.find(entity);
    if (remembered == nullptr) {
      return;
    }
    const double age = secondsBetween(remembered->lastSeen, now, secondsPerTick);
    if (age >= config.memorySeconds) {
      return;
    }
    Observation fading = *remembered;
    fading.confidence = 1.0 - (age / config.memorySeconds);
    observations.push_back(fading);
  };

  observe(ObservedEntity::ball(), state.ball().position, state.ball().velocity);
  for (const PlayerMatchState& other : state.players()) {
    if (other.playerId != observer.playerId) {
      observe(ObservedEntity::player(other.playerId), other.position, other.velocity);
    }
  }
  std::ranges::sort(observations, {}, &Observation::entity);
}

std::vector<RememberedPlayer> rememberedPlayers(const MatchState& state,
                                                const std::size_t observerIndex, const SimTick now,
                                                const double secondsPerTick,
                                                const PerceptionConfig& config,
                                                const double minConfidence) {
  const TeamSide side = state.players()[observerIndex].side;
  std::vector<RememberedPlayer> remembered;
  for (const Observation& observation : state.perception(observerIndex).observations) {
    if (observation.entity.isBall() || observation.confidence < minConfidence) {
      continue;
    }
    const auto index = findPlayerIndex(state, observation.entity.playerId());
    if (!index) {
      continue;
    }
    remembered.push_back({.index = *index,
                          .playerId = observation.entity.playerId(),
                          .position = estimatePosition(observation, now, secondsPerTick, config),
                          .velocity = observation.velocity,
                          .confidence = observation.confidence,
                          .teammate = state.players()[*index].side == side});
  }
  return remembered;
}

MatchSystem makePerceptionSystem(const PerceptionConfig& config) {
  validate(config);
  return {.name = std::string(kPerceptionSystemName),
          .update =
              [config](const MatchStepContext& context, const MatchState& current,
                       MatchStateWriter& next) {
                for (std::size_t index = 0; index < current.players().size(); ++index) {
                  perceive(current, index, context.tick(), context.secondsPerTick(), config,
                           next.perception(index));
                }
              },
          .intervalTicks = config.intervalTicks,
          .phaseTicks = 0};
}

}  // namespace ElyverseFootball::SimMatch
