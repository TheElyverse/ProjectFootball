#pragma once

#include <string_view>

#include "matchSimulation.hpp"
#include "matchState.hpp"
#include "pitch.hpp"

namespace ElyverseFootball::SimMatch {

// Rolling resistance of a ball on grass used when a caller does not state
// one. A ground pass leaving the foot at 10 m/s rolls 10² / (2 · 1.5) ≈ 33 m.
inline constexpr double kDefaultRollingDeceleration = 1.5;  // m/s²

// The physical constants of the ball model. The rolling deceleration must be
// positive and finite.
struct BallPhysics {
  double rollingDeceleration = kDefaultRollingDeceleration;

  friend bool operator==(const BallPhysics&, const BallPhysics&) = default;
};

// One tick of a free, rolling ball as described in docs/ball-movement.md:
// ground friction slows it at a constant rate along its direction of travel
// until it stops -- never reversing it -- and a ball that leaves the pitch
// stops on the line where it crossed it.
//
// The integration is exact for constant deceleration, so a ball rolling from
// speed v stops after v² / (2 · rollingDeceleration) meters, independent of
// the tick rate.
[[nodiscard]] BallState stepFreeBall(const BallState& ball, const BallPhysics& physics,
                                     const Pitch& pitch, double secondsPerTick) noexcept;

// Distance a ball rolling at this speed covers before it stops, in meters.
[[nodiscard]] double rollingDistance(double speed, const BallPhysics& physics) noexcept;

inline constexpr std::string_view kBallMovementSystemName = "ball movement";

// Moves the ball one tick with stepFreeBall(). Writes the ball's position and
// velocity, every tick. Throws std::invalid_argument for a rolling
// deceleration that is not positive and finite.
[[nodiscard]] MatchSystem makeBallMovementSystem(BallPhysics physics);

}  // namespace ElyverseFootball::SimMatch
