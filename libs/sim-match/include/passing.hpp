#pragma once

#include <cstddef>

#include "ballMovement.hpp"
#include "matchState.hpp"
#include "random.hpp"
#include "vec2.hpp"

namespace ElyverseFootball::SimMatch {

// How players kick ground passes; see docs/passing.md. Part of MatchConfig
// and therefore of every replay.
struct PassConfig {
  // The speed a planned pass should still have when it reaches its target.
  double arrivalSpeed = 4.0;  // m/s
  // The hardest ground pass a player can kick.
  double maxSpeed = 22.0;  // m/s
  // Execution error: the ball leaves the foot up to this far off line per
  // meter along it, uniformly distributed -- 0.03 is about ±1.7 degrees.
  double directionError = 0.03;
  // Execution error: the kick is up to this fraction harder or softer than
  // intended, uniformly distributed.
  double speedError = 0.05;
  // An opponent this close to the passer puts him under pressure: none at
  // the edge, full pressure at his feet.
  double pressureRadius = 3.0;  // m
  // Under full pressure both execution errors grow by this factor: 1 doubles
  // them. Without pressure they stay as they are.
  double pressureErrorFactor = 1.0;

  friend bool operator==(const PassConfig&, const PassConfig&) = default;
};

// The speed a pass over this distance needs to arrive at arrivalSpeed:
// sqrt(arrivalSpeed² + 2 · rollingDeceleration · distance), capped at
// maxSpeed. A capped pass arrives slower, or stops short when even maxSpeed
// cannot reach -- see passReach().
[[nodiscard]] double planPassSpeed(double distance, const BallPhysics& physics,
                                   const PassConfig& config) noexcept;

// How far a pass kicked at this speed rolls before it stops: the ball's
// rolling distance.
[[nodiscard]] double passReach(double speed, const BallPhysics& physics) noexcept;

// The velocity the ball leaves the passer's foot with: toward the intent's
// target from where the ball is, at the intended speed, both off by a random
// execution error drawn from random -- always two draws -- and never faster
// than maxSpeed. The errors grow by pressureErrorFactor * pressure, for a
// pressure in [0, 1] (passPressure()). A target on the ball itself is played
// along the passer's facing. This is the how-well half of a pass; the what is
// the PassIntent.
[[nodiscard]] SimCore::Vec2 executePass(const PassIntent& intent, const BallState& ball,
                                        const PlayerMatchState& passer, const PassConfig& config,
                                        SimCore::RandomNumberGenerator& random,
                                        double pressure = 0.0) noexcept;

// How much pressure the passer at this index is under, in [0, 1]: 1 - d /
// pressureRadius for the nearest opponent at distance d, 0 without one
// within the radius. Pressure is physical, so it uses true positions.
[[nodiscard]] double passPressure(const MatchState& state, std::size_t passerIndex,
                                  const PassConfig& config) noexcept;

// Throws std::invalid_argument unless every value is finite, the speeds and
// the pressure radius are positive, the errors and the pressure factor are
// not negative, and a kick under full pressure keeps some speed: speedError *
// (1 + pressureErrorFactor) below 1.
void validate(const PassConfig& config);

}  // namespace ElyverseFootball::SimMatch
