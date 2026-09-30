#include "goalFrame.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>

#include "reception.hpp"
#include "stableMath.hpp"

namespace ElyverseFootball::SimMatch {
namespace {

using SimCore::Vec2;

// Halvings of the interval a moment is searched in: enough to reach the last
// bits of a double, at a fixed cost (see ballPhysics.cpp).
inline constexpr int kSearchIterations = 64;

// Moments at which the ball is checked against the crossbar while it passes
// the goal line. It passes within a few centimeters of flight, so the steps
// are far smaller than the bar is thick.
inline constexpr int kCrossbarSamples = 16;

// How long a ball is followed to the goal line: no ball rolls or flies
// longer.
inline constexpr double kCrossingHorizonSeconds = 10.0;

// A direction in space: along the pitch plane and up.
struct Vec3 {
  Vec2 plane;
  double up = 0.0;

  [[nodiscard]] double dot(const Vec3& other) const noexcept {
    return plane.dot(other.plane) + (up * other.up);
  }
};

// A free ball and how long it is followed.
struct Flight {
  BallState ball;
  BallPhysics physics;
  double seconds = 0.0;

  [[nodiscard]] BallState after(const double moment) const noexcept {
    return ballAfter(ball, physics, moment);
  }

  // How far along the ground the ball is from where it started, this long
  // on. A free ball moves along a straight line in the pitch plane and never
  // back, so this only grows.
  [[nodiscard]] double travelled(const double moment) const noexcept {
    return SimCore::distance(ball.position, after(moment).position);
  }

  // When within the flight the ball has travelled this far along the ground.
  [[nodiscard]] double secondsToTravel(const double meters) const noexcept {
    double low = 0.0;
    double high = seconds;
    for (int step = 0; step < kSearchIterations; ++step) {
      const double middle = low + ((high - low) / 2.0);
      if (travelled(middle) < meters) {
        low = middle;
      } else {
        high = middle;
      }
    }
    return high;
  }
};

// The ball leaving the frame it touches with its centre `normal` away from
// the frame's axis; empty if it is not moving into it.
[[nodiscard]] std::optional<BallState> rebound(const BallState& ball, const Vec3& normal,
                                               const WoodworkConfig& config) noexcept {
  const Vec3 velocity{.plane = ball.velocity, .up = ball.verticalVelocity};
  const double into = velocity.dot(normal);
  if (into >= 0.0) {
    return std::nullopt;
  }
  const double push = -(1.0 + config.restitution) * into;
  BallState after = ball;
  after.velocity = ball.velocity + (normal.plane * push);
  after.verticalVelocity = ball.verticalVelocity + (normal.up * push);
  after.spin = 0.0;
  return after;
}

// The hit of a post standing at `post`, if the ball's way along the ground to
// `end` brings it against it below `top`, the top of the crossbar.
[[nodiscard]] std::optional<WoodworkHit> findPostHit(const Flight& flight, const BallState& end,
                                                     const Vec2 post, const double top,
                                                     const WoodworkConfig& config) noexcept {
  const BallState& ball = flight.ball;
  const double reach = config.radius + kBallRadius;
  const auto contact = findContact(post, post, ball.position, end.position, reach);
  if (!contact) {
    return std::nullopt;
  }
  const double way = SimCore::distance(ball.position, end.position) * contact->contactFraction;
  const double moment = flight.secondsToTravel(way);
  const BallState touching = flight.after(moment);
  if (touching.height > top) {
    return std::nullopt;
  }
  const Vec2 offset = touching.position - post;
  const double length = offset.length();
  if (length <= 0.0) {
    return std::nullopt;
  }
  const auto after = rebound(touching, {.plane = offset * (1.0 / length), .up = 0.0}, config);
  if (!after) {
    return std::nullopt;
  }
  return WoodworkHit{.end = GoalEnd::kMinX,
                     .part = WoodworkPart::kPostAtMinY,
                     .seconds = moment,
                     .ball = touching,
                     .rebound = *after};
}

// The hit of a goal's crossbar, if the ball comes against it between the
// posts while it passes the goal line on its way to `end`.
[[nodiscard]] std::optional<WoodworkHit> findCrossbarHit(const Flight& flight, const BallState& end,
                                                         const Goal& goal,
                                                         const WoodworkConfig& config) noexcept {
  const BallState& ball = flight.ball;
  const double reach = config.radius + kBallRadius;
  const double axisHeight = goal.heightMeters + config.radius;
  const double way = SimCore::distance(ball.position, end.position);
  const double alongX = (end.position.x - ball.position.x) / way;
  // The stretch of the way on which the ball is within reach of the goal
  // line: only there can it touch the bar.
  double from = 0.0;
  double until = way;
  if (alongX != 0.0) {
    const double first = (goal.center.x - reach - ball.position.x) / alongX;
    const double second = (goal.center.x + reach - ball.position.x) / alongX;
    from = std::max(from, std::min(first, second));
    until = std::min(until, std::max(first, second));
  }
  if (from > until) {
    return std::nullopt;
  }
  const double start = flight.secondsToTravel(from);
  const double stop = flight.secondsToTravel(until);
  // How far the ball's surface is from the bar's; negative inside it, and
  // never inside beside the posts.
  const auto gap = [&](const double moment) {
    const BallState there = flight.after(moment);
    if (!goal.isBetweenPosts(there.position.y)) {
      return std::numeric_limits<double>::infinity();
    }
    return SimCore::stableHypot(there.position.x - goal.center.x,
                                there.height + kBallRadius - axisHeight) -
           reach;
  };
  double clear = start;
  double moment = start;
  bool touches = gap(start) <= 0.0;
  for (int sample = 1; !touches && sample <= kCrossbarSamples; ++sample) {
    moment = start + ((stop - start) * static_cast<double>(sample) / kCrossbarSamples);
    touches = gap(moment) <= 0.0;
    if (!touches) {
      clear = moment;
    }
  }
  if (!touches) {
    return std::nullopt;
  }
  for (int step = 0; step < kSearchIterations; ++step) {
    const double middle = clear + ((moment - clear) / 2.0);
    if (gap(middle) <= 0.0) {
      moment = middle;
    } else {
      clear = middle;
    }
  }
  const BallState touching = flight.after(moment);
  const double offsetX = touching.position.x - goal.center.x;
  const double offsetUp = touching.height + kBallRadius - axisHeight;
  const double length = SimCore::stableHypot(offsetX, offsetUp);
  if (length <= 0.0) {
    return std::nullopt;
  }
  const auto after = rebound(
      touching, {.plane = {.x = offsetX / length, .y = 0.0}, .up = offsetUp / length}, config);
  if (!after) {
    return std::nullopt;
  }
  return WoodworkHit{.end = GoalEnd::kMinX,
                     .part = WoodworkPart::kCrossbar,
                     .seconds = moment,
                     .ball = touching,
                     .rebound = *after};
}

}  // namespace

void validate(const WoodworkConfig& config) {
  const bool valid = config.radius > 0.0 && config.radius <= std::numeric_limits<double>::max() &&
                     config.restitution >= 0.0 && config.restitution <= 1.0;
  if (!valid) {
    throw std::invalid_argument(
        "woodwork: the radius must be positive and finite and the restitution between 0 and 1");
  }
}

std::string_view woodworkPartName(const WoodworkPart part) noexcept {
  switch (part) {
    case WoodworkPart::kPostAtMinY:
      return "postAtMinY";
    case WoodworkPart::kPostAtMaxY:
      return "postAtMaxY";
    case WoodworkPart::kCrossbar:
      return "crossbar";
  }
  return "unknown";
}

std::optional<WoodworkHit> findWoodworkHit(const BallState& ball, const BallPhysics& physics,
                                           const Pitch& pitch, const WoodworkConfig& config,
                                           const double seconds) noexcept {
  const Flight flight{.ball = ball, .physics = physics, .seconds = seconds};
  const BallState end = flight.after(seconds);
  if (end.position == ball.position) {
    return std::nullopt;
  }
  const double reach = config.radius + kBallRadius;
  std::optional<WoodworkHit> first;
  const auto consider = [&first](std::optional<WoodworkHit> hit, const GoalEnd goalEnd,
                                 const WoodworkPart part) {
    if (hit && (!first || hit->seconds < first->seconds)) {
      hit->end = goalEnd;
      hit->part = part;
      first = hit;
    }
  };
  for (const GoalEnd goalEnd : {GoalEnd::kMinX, GoalEnd::kMaxX}) {
    const Goal goal = pitch.goal(goalEnd);
    // Almost every ball is nowhere near a goal line: nothing to search.
    const auto [nearest, farthest] = std::minmax(ball.position.x, end.position.x);
    if (nearest > goal.center.x + reach || farthest < goal.center.x - reach) {
      continue;
    }
    const double top = goal.heightMeters + (2.0 * config.radius);
    consider(findPostHit(flight, end, goal.postAtMinY(), top, config), goalEnd,
             WoodworkPart::kPostAtMinY);
    consider(findPostHit(flight, end, goal.postAtMaxY(), top, config), goalEnd,
             WoodworkPart::kPostAtMaxY);
    consider(findCrossbarHit(flight, end, goal, config), goalEnd, WoodworkPart::kCrossbar);
  }
  return first;
}

std::optional<GoalLineCrossing> predictGoalLineCrossing(const BallState& ball,
                                                        const BallPhysics& physics,
                                                        const Pitch& pitch,
                                                        const GoalEnd end) noexcept {
  const double speed = ball.velocity.length();
  const double towardLine = pitch.goalLineX(end) - ball.position.x;
  if (speed <= 0.0 || towardLine * ball.velocity.x <= 0.0) {
    return std::nullopt;
  }
  const double way = towardLine / (ball.velocity.x / speed);
  const Flight flight{.ball = ball, .physics = physics, .seconds = kCrossingHorizonSeconds};
  if (flight.travelled(flight.seconds) < way) {
    return std::nullopt;
  }
  const BallState crossing = flight.after(flight.secondsToTravel(way));
  return GoalLineCrossing{.y = crossing.position.y, .height = crossing.height};
}

}  // namespace ElyverseFootball::SimMatch
