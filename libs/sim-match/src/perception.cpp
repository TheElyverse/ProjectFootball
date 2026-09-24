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
  // vector, so no angle needs to be computed per entity. std::cos(pi / 2) is
  // about 6e-17, not 0, so without the tolerance a player straight sideways
  // would fall outside a 180 degree cone despite the inclusive edge; the
  // tolerance also absorbs a last-bit difference between standard libraries.
  constexpr double kEdgeTolerance = 1e-9;
  const double halfAngle = config.fieldOfViewDegrees * std::numbers::pi / 360.0;
  return observer.facing.dot(offset) >=
         std::sqrt(distanceSquared) * (std::cos(halfAngle) - kEdgeTolerance);
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
  const auto observe = [&](const ObservedEntity entity, const Vec2 position, const Vec2 velocity) {
    if (canSee(observer, position, config)) {
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
