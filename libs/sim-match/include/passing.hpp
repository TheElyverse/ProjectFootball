#pragma once

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
// than maxSpeed. A target on the ball itself is played along the passer's
// facing. This is the how-well half of a pass; the what is the PassIntent.
[[nodiscard]] SimCore::Vec2 executePass(const PassIntent& intent, const BallState& ball,
                                        const PlayerMatchState& passer, const PassConfig& config,
                                        SimCore::RandomNumberGenerator& random) noexcept;

// Throws std::invalid_argument unless every value is finite, the speeds are
// positive and the errors are not negative.
void validate(const PassConfig& config);

}  // namespace ElyverseFootball::SimMatch
