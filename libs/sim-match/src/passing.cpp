#include "passing.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace ElyverseFootball::SimMatch {
namespace {

using SimCore::Vec2;

// A uniform draw in [-1, 1).
[[nodiscard]] double symmetricUniform(SimCore::RandomNumberGenerator& random) noexcept {
  return (2.0 * random.nextUniform()) - 1.0;
}

[[nodiscard]] bool isFinite(const double value) noexcept {
  return std::abs(value) <= std::numeric_limits<double>::max();
}

}  // namespace

double planPassSpeed(const double distance, const BallPhysics& physics,
                     const PassConfig& config) noexcept {
  const double needed = std::sqrt((config.arrivalSpeed * config.arrivalSpeed) +
                                  (2.0 * physics.rollingDeceleration * distance));
  return std::min(needed, config.maxSpeed);
}

double passReach(const double speed, const BallPhysics& physics) noexcept {
  return rollingDistance(speed, physics);
}

Vec2 executePass(const PassIntent& intent, const BallState& ball, const PlayerMatchState& passer,
                 const PassConfig& config, SimCore::RandomNumberGenerator& random,
                 const double pressure) noexcept {
  // Without pressure exactly 1, so an unpressed pass is the same bits as
  // before pressure existed.
  const double spoil = 1.0 + (config.pressureErrorFactor * pressure);
  // Drawn first and always, so the stream advances the same way for every
  // pass, whatever its target.
  const double lateral = symmetricUniform(random) * (config.directionError * spoil);
  const double strength = 1.0 + (symmetricUniform(random) * (config.speedError * spoil));

  const Vec2 offset = intent.target - ball.position;
  const double distance = std::sqrt(offset.lengthSquared());
  const Vec2 line = distance > 0.0 ? offset * (1.0 / distance) : passer.facing;
  // Off line by `lateral` meters per meter along it, then back to unit length.
  const Vec2 perpendicular{.x = -line.y, .y = line.x};
  const Vec2 skewed = line + (perpendicular * lateral);
  const Vec2 direction = skewed * (1.0 / std::sqrt(skewed.lengthSquared()));

  const double speed = std::min(intent.speed * strength, config.maxSpeed);
  return direction * speed;
}

double passPressure(const MatchState& state, const std::size_t passerIndex,
                    const PassConfig& config) noexcept {
  const PlayerMatchState& passer = state.players()[passerIndex];
  double pressure = 0.0;
  for (const PlayerMatchState& other : state.players()) {
    if (other.side != passer.side) {
      const double distance = std::sqrt((other.position - passer.position).lengthSquared());
      pressure = std::max(pressure, 1.0 - (distance / config.pressureRadius));
    }
  }
  return pressure;
}

void validate(const PassConfig& config) {
  const bool valid = config.arrivalSpeed > 0.0 && isFinite(config.arrivalSpeed) &&
                     config.maxSpeed > 0.0 && isFinite(config.maxSpeed) &&
                     config.directionError >= 0.0 && isFinite(config.directionError) &&
                     config.speedError >= 0.0 && config.pressureRadius > 0.0 &&
                     isFinite(config.pressureRadius) && config.pressureErrorFactor >= 0.0 &&
                     isFinite(config.pressureErrorFactor) &&
                     config.speedError * (1.0 + config.pressureErrorFactor) < 1.0;
  if (!valid) {
    throw std::invalid_argument("passing: invalid configuration");
  }
}

}  // namespace ElyverseFootball::SimMatch
