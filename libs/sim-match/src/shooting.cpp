#include "shooting.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

#include "randomDraws.hpp"

namespace ElyverseFootball::SimMatch {
namespace {

using RandomDraws::symmetricTriangular;
using RandomDraws::symmetricUniform;
using SimCore::unitOr;
using SimCore::Vec2;

[[nodiscard]] bool isFiniteNonNegative(const double value) noexcept {
  return value >= 0.0 && value <= std::numeric_limits<double>::max();
}

[[nodiscard]] bool isPositiveFinite(const double value) noexcept {
  return value > 0.0 && value <= std::numeric_limits<double>::max();
}

[[nodiscard]] bool isShare(const double value) noexcept {
  return value >= 0.0 && value <= 1.0;
}

// However badly a ball is struck, it leaves the foot at no less than half and
// no more than one and a half times the intended speed.
inline constexpr double kMaxSpeedError = 0.5;

}  // namespace

void validate(const ShotConfig& config) {
  const bool valid =
      isFiniteNonNegative(config.spreadAtZero) && isFiniteNonNegative(config.spreadPerMeter) &&
      isFiniteNonNegative(config.speedError) && isPositiveFinite(config.maxLaunchSlope) &&
      isFiniteNonNegative(config.topspin) && isFiniteNonNegative(config.spinError) &&
      isFiniteNonNegative(config.pressureErrorFactor) &&
      isFiniteNonNegative(config.balanceErrorFactor) &&
      isFiniteNonNegative(config.weakFootErrorFactor) &&
      isFiniteNonNegative(config.unsettledErrorFactor) &&
      isFiniteNonNegative(config.weakFootSide) && isPositiveFinite(config.unsettledSeconds) &&
      isPositiveFinite(config.unsettledBallSpeed) && isFiniteNonNegative(config.deflectionSpeed) &&
      isPositiveFinite(config.blockRadius) && isFiniteNonNegative(config.blockReach) &&
      isShare(config.minDeflectedSpeed) && isShare(config.maxDeflectedSpeed) &&
      config.minDeflectedSpeed <= config.maxDeflectedSpeed && isShare(config.blockedBelow) &&
      isFiniteNonNegative(config.deflectionSpread) && isFiniteNonNegative(config.deflectionLift);
  if (!valid) {
    throw std::invalid_argument("shooting: invalid configuration");
  }
}

double skillErrorFactor(const double skill) noexcept {
  constexpr double kPerfect = 0.1;
  return kPerfect + (2.0 * (1.0 - kPerfect) * (1.0 - skill));
}

ShotConditions shotConditions(const MatchState& state, const std::size_t shooterIndex,
                              const Vec2 from, const ShotIntent& intent, const SimCore::SimTick now,
                              const double secondsPerTick, const ShotConfig& config,
                              const PassConfig& passing) {
  const PlayerMatchState& shooter = state.players()[shooterIndex];
  const Vec2 line = unitOr(intent.target - from, shooter.facing);
  ShotConditions conditions;
  conditions.pressure = passPressure(state, shooterIndex, passing);

  // What he moves at other than toward his shot, of all he can run.
  const double toward = std::max(0.0, shooter.velocity.dot(line));
  conditions.imbalance =
      std::min(1.0, (shooter.velocity - (line * toward)).length() / shooter.attributes.maxSpeed);

  // Positive to his left.
  const double side = (shooter.facing.x * line.y) - (shooter.facing.y * line.x);
  conditions.weakFoot = shooter.attributes.strongFoot == Foot::kRight ? side > config.weakFootSide
                                                                      : side < -config.weakFootSide;

  // His reception counts while it is his last touch: a ball he lost and won
  // back since is not the one he received.
  if (const auto& reception = state.lastReception();
      reception && reception->player == shooter.playerId &&
      state.ball().lastTouch == BallTouch{.playerId = reception->player, .tick = reception->tick}) {
    const double since =
        static_cast<double>(now.value() - reception->tick.value()) * secondsPerTick;
    if (since >= 0.0 && since < config.unsettledSeconds) {
      conditions.unsettled = std::min(1.0, reception->ballSpeed / config.unsettledBallSpeed) *
                             (1.0 - (since / config.unsettledSeconds));
    }
  }
  return conditions;
}

double shotErrorFactor(const ShotConditions& conditions, const PlayerAttributes& attributes,
                       const ShotConfig& config) noexcept {
  const double foot =
      conditions.weakFoot ? config.weakFootErrorFactor * (1.0 - attributes.weakFootAccuracy) : 0.0;
  return (1.0 + (config.pressureErrorFactor * conditions.pressure)) *
         (1.0 + (config.balanceErrorFactor * conditions.imbalance)) * (1.0 + foot) *
         (1.0 + (config.unsettledErrorFactor * conditions.unsettled));
}

ShotStrike executeShot(const ShotIntent& intent, const Vec2 from, const PlayerMatchState& shooter,
                       const double errorFactor, const ShotConfig& config,
                       const BallPhysics& physics,
                       SimCore::RandomNumberGenerator& random) noexcept {
  const double accuracy = skillErrorFactor(shooter.attributes.shotAccuracy);
  const double technique = skillErrorFactor(shooter.attributes.shotTechnique);
  const double spread =
      (config.spreadAtZero + (config.spreadPerMeter * SimCore::distance(from, intent.target))) *
      accuracy * errorFactor;
  // Drawn first and always, so the stream advances the same way for every
  // shot, whatever its target.
  const double across = symmetricTriangular(random) * spread;
  const double upward = symmetricTriangular(random) * spread * technique;
  const double strength =
      1.0 + (symmetricUniform(random) *
             std::min(config.speedError * technique * errorFactor, kMaxSpeedError));
  const double spinOff = symmetricUniform(random) * config.spinError * technique * errorFactor;

  ShotStrike strike;
  // The aim is a point on the goal line, so it strays along that line.
  strike.target = {.x = intent.target.x, .y = intent.target.y + across};
  strike.height = std::max(0.0, intent.height + upward);
  const Vec2 offset = strike.target - from;
  const double slope = config.maxLaunchSlope * intent.speed;
  // Aimed at the grass, or below it, the ball is struck along it and rolls.
  const Launch launch{.speed = intent.speed, .distance = offset.length(), .height = strike.height};
  const double lift =
      strike.height > 0.0 ? launchVerticalVelocity(launch, physics).value_or(slope) : 0.0;
  strike.velocity =
      unitOr(offset, shooter.facing) * std::min(intent.speed * strength, kMaxBallSpeed);
  strike.verticalVelocity = std::min(std::min(lift, slope) * strength, kMaxBallSpeed);
  strike.spin = std::clamp((config.topspin * shooter.attributes.shotTechnique) + spinOff,
                           -kMaxBallSpin, kMaxBallSpin);
  return strike;
}

Deflection deflectShot(const BallState& ball, const ShotConfig& config,
                       SimCore::RandomNumberGenerator& random) noexcept {
  const double kept =
      config.minDeflectedSpeed +
      (random.nextUniform() * (config.maxDeflectedSpeed - config.minDeflectedSpeed));
  const double side = symmetricUniform(random) * config.deflectionSpread;
  const double lift = random.nextUniform() * config.deflectionLift;

  Deflection deflection{.ball = ball, .blocked = kept < config.blockedBelow};
  const double speed = ball.velocity.length();
  if (speed > 0.0) {
    const Vec2 line = ball.velocity * (1.0 / speed);
    const Vec2 turned = line + (Vec2{.x = -line.y, .y = line.x} * side);
    deflection.ball.velocity = turned * (speed * kept / turned.length());
  }
  deflection.ball.verticalVelocity =
      std::clamp((ball.verticalVelocity * kept) + lift, -kMaxBallSpeed, kMaxBallSpeed);
  deflection.ball.spin = 0.0;
  return deflection;
}

}  // namespace ElyverseFootball::SimMatch
