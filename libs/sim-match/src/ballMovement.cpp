#include "ballMovement.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>

namespace ElyverseFootball::SimMatch {
namespace {

using SimCore::Vec2;

// The fraction t in (0, 1] of the move from start to end at which the ball
// crosses the pitch boundary, for a start on the pitch and an end off it.
// Checked per boundary line; the earliest crossing is where the ball leaves.
[[nodiscard]] double exitFraction(const Vec2 start, const Vec2 end, const Pitch& pitch) noexcept {
  double fraction = 1.0;
  const auto crossing = [&fraction](const double from, const double until, const double line) {
    const bool crosses = (from <= line && until > line) || (from >= line && until < line);
    if (crosses) {
      fraction = std::min(fraction, (line - from) / (until - from));
    }
  };
  crossing(start.x, end.x, 0.0);
  crossing(start.x, end.x, pitch.lengthMeters());
  crossing(start.y, end.y, 0.0);
  crossing(start.y, end.y, pitch.widthMeters());
  return fraction;
}

}  // namespace

double rollingDistance(const double speed, const BallPhysics& physics) noexcept {
  return (speed * speed) / (2.0 * physics.rollingDeceleration);
}

BallState stepFreeBall(const BallState& ball, const BallPhysics& physics, const Pitch& pitch,
                       const double secondsPerTick) noexcept {
  // std::sqrt rather than std::hypot: correctly rounded on every platform.
  const double speed = std::sqrt(ball.velocity.lengthSquared());
  if (speed == 0.0) {
    return ball;
  }
  const Vec2 direction = ball.velocity * (1.0 / speed);

  // Constant deceleration along the direction of travel. A ball that stops
  // within the tick covers exactly its remaining rolling distance and rests;
  // otherwise it moves at the average of its start and end speed.
  const double speedLoss = physics.rollingDeceleration * secondsPerTick;
  double endSpeed = 0.0;
  double distance = rollingDistance(speed, physics);
  if (speed > speedLoss) {
    endSpeed = speed - speedLoss;
    distance = (speed + endSpeed) / 2.0 * secondsPerTick;
  }
  const Vec2 end = ball.position + (direction * distance);

  // Out of play: a ball that leaves the pitch stops on the line where it
  // crossed it. The clamp removes rounding that would leave it a hair off the
  // line. A ball already off the pitch (a hand-built state) rolls on.
  if (pitch.contains(ball.position) && !pitch.contains(end)) {
    const double fraction = exitFraction(ball.position, end, pitch);
    return {.position = pitch.clamp(ball.position + ((end - ball.position) * fraction)),
            .velocity = {},
            .owner = ball.owner};
  }
  return {.position = end, .velocity = direction * endSpeed, .owner = ball.owner};
}

MatchSystem makeBallMovementSystem(const BallPhysics physics) {
  const bool valid = physics.rollingDeceleration > 0.0 &&
                     physics.rollingDeceleration <= std::numeric_limits<double>::max();
  if (!valid) {
    throw std::invalid_argument("ball movement: rolling deceleration must be positive and finite");
  }
  return {.name = std::string(kBallMovementSystemName),
          .update = [physics](const MatchStepContext& context, const MatchState& current,
                              MatchStateWriter& next) {
            const BallState moved =
                stepFreeBall(current.ball(), physics, current.pitch(), context.secondsPerTick());
            next.setBallPosition(moved.position);
            next.setBallVelocity(moved.velocity);
          }};
}

}  // namespace ElyverseFootball::SimMatch
