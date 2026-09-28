#include "ballPhysics.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

#include "stableMath.hpp"

namespace ElyverseFootball::SimMatch {
namespace {

using SimCore::Vec2;

// Halvings of the interval a landing is searched in. The interval shrinks by a
// factor of two per step, so this many steps reach the last bits of a double
// for any tick: the search is exact rather than approximate, and its cost is
// fixed, which is what a replay needs.
inline constexpr int kLandingIterations = 64;

// How often predictBallLanding() may double its horizon before it gives up.
// One second doubled twenty times is longer than a week; only a ball without
// gravity never comes down.
inline constexpr int kMaxHorizonDoublings = 20;

// Bounces resolved within a single tick. A ball hopping this often in a
// thirtieth of a second has all but stopped hopping, and the bound keeps a
// wildly tuned configuration from making a step run long.
inline constexpr int kMaxBouncesPerTick = 8;

[[nodiscard]] bool isFiniteNonNegative(const double value) noexcept {
  return value >= 0.0 && value <= std::numeric_limits<double>::max();
}

[[nodiscard]] bool isPositiveFinite(const double value) noexcept {
  return value > 0.0 && value <= std::numeric_limits<double>::max();
}

[[nodiscard]] bool isShare(const double value) noexcept {
  return value >= 0.0 && value <= 1.0;
}

// The ball `seconds` into its flight, in closed form: gravity and a linear air
// drag. Horizontal speed decays as e^{-k·t}; the vertical velocity approaches
// the terminal speed g / k from wherever it starts, and the height is the
// integral of that. Spin dies away the same exponential way.
//
// Exact for any span, so the same flight comes out the same however the ticks
// are cut -- the one thing a numerical integrator could not promise. It knows
// nothing about the ground: the caller splits the span at the landing.
[[nodiscard]] BallState flownFor(const BallState& ball, const BallPhysics& physics,
                                 const double seconds) noexcept {
  BallState flown = ball;
  flown.spin = ball.spin * SimCore::stableExp(-physics.spinDecay * seconds);
  if (physics.airDrag <= 0.0) {
    // Without drag the flight is the plain parabola.
    flown.position = ball.position + (ball.velocity * seconds);
    flown.height = ball.height + (ball.verticalVelocity * seconds) -
                   (0.5 * physics.gravity * seconds * seconds);
    flown.verticalVelocity = ball.verticalVelocity - (physics.gravity * seconds);
    return flown;
  }
  const double decay = SimCore::stableExp(-physics.airDrag * seconds);
  // The integral of the decay over the span: how far a unit speed carries.
  const double span = (1.0 - decay) / physics.airDrag;
  const double terminal = physics.gravity / physics.airDrag;
  flown.position = ball.position + (ball.velocity * span);
  flown.velocity = ball.velocity * decay;
  flown.height = ball.height + ((ball.verticalVelocity + terminal) * span) - (terminal * seconds);
  flown.verticalVelocity = ((ball.verticalVelocity + terminal) * decay) - terminal;
  return flown;
}

// When within (0, seconds] the flight reaches the ground, for a flight that is
// on or below it at the end of the span. A flight rises at most once and then
// falls, so "already down" is monotone in time and bisection finds the single
// crossing.
[[nodiscard]] double landingSeconds(const BallState& ball, const BallPhysics& physics,
                                    const double seconds) noexcept {
  double low = 0.0;
  double high = seconds;
  for (int step = 0; step < kLandingIterations; ++step) {
    const double middle = low + ((high - low) / 2.0);
    if (flownFor(ball, physics, middle).height > 0.0) {
      low = middle;
    } else {
      high = middle;
    }
  }
  return high;
}

// The ball as it leaves the ground it just landed on: it keeps
// bounceRestitution of its vertical speed, bounceGrip of its speed along the
// ground, and spends spinTransfer of its spin driving itself on -- topspin
// forward, backspin against the travel, never far enough to turn it around. A
// ball dropping straight down has no direction to drive along and keeps none.
[[nodiscard]] BallState bounced(const BallState& landed, const BallPhysics& physics) noexcept {
  BallState after = landed;
  after.height = 0.0;
  after.verticalVelocity = -physics.bounceRestitution * landed.verticalVelocity;
  const double speed = landed.velocity.length();
  if (speed > 0.0) {
    const Vec2 direction = landed.velocity * (1.0 / speed);
    const double driven =
        (speed * physics.bounceGrip) + (physics.spinTransfer * landed.spin * kBallRadius);
    after.velocity = direction * std::clamp(driven, 0.0, kMaxBallSpeed);
  }
  after.spin = landed.spin * (1.0 - physics.spinTransfer);
  return after;
}

// The ball rolled for this span, without the pitch boundary: constant
// deceleration along its direction of travel, exactly integrated, so it stops
// after its rolling distance whatever the span. It never reverses, and a ball
// at rest stays as it is, spin and all.
[[nodiscard]] BallState rolled(const BallState& ball, const BallPhysics& physics,
                               const double seconds) noexcept {
  const double speed = ball.velocity.length();
  if (speed == 0.0) {
    return ball;
  }
  const Vec2 direction = ball.velocity * (1.0 / speed);
  const double speedLoss = physics.rollingDeceleration * seconds;
  double endSpeed = 0.0;
  double distance = rollingDistance(speed, physics);
  if (speed > speedLoss) {
    endSpeed = speed - speedLoss;
    distance = (speed + endSpeed) / 2.0 * seconds;
  }
  BallState moved = ball;
  moved.position = ball.position + (direction * distance);
  moved.velocity = direction * endSpeed;
  return moved;
}

// A ball that left the pitch stops on the line where it crossed it, on the
// ground and at rest; one that stayed on is left as the move made it. The
// clamp removes rounding that would leave it a hair off the line. A ball
// already off the pitch -- only possible in a hand-built state -- plays on.
[[nodiscard]] BallState withPitchBoundary(const BallState& before, const BallState& after,
                                          const Pitch& pitch) noexcept {
  if (!pitch.contains(before.position) || pitch.contains(after.position)) {
    return after;
  }
  const double fraction = pitchExitFraction(before.position, after.position, pitch);
  BallState stopped = before;
  stopped.position = pitch.clamp(before.position + ((after.position - before.position) * fraction));
  stopped.velocity = {};
  stopped.height = 0.0;
  stopped.verticalVelocity = 0.0;
  stopped.spin = 0.0;
  return stopped;
}

// One tick of a flying ball: fly to the end of the tick, or to the ground and
// on from there. The tick is split at the landing rather than at its end,
// which is what makes a flight independent of the tick rate.
[[nodiscard]] BallState flightStep(const BallState& ball, const BallPhysics& physics,
                                   const Pitch& pitch, const double secondsPerTick) noexcept {
  BallState current = ball;
  double remaining = secondsPerTick;
  for (int bounces = 0; bounces < kMaxBouncesPerTick; ++bounces) {
    if (remaining <= 0.0) {
      return withPitchBoundary(ball, current, pitch);
    }
    const BallState flown = flownFor(current, physics, remaining);
    if (flown.height > 0.0) {
      return withPitchBoundary(ball, flown, pitch);
    }
    const double landing = landingSeconds(current, physics, remaining);
    current = bounced(flownFor(current, physics, landing), physics);
    remaining -= landing;
    // Too slow to leave the ground again: the ball stays down and rolls what
    // is left of the tick.
    if (current.verticalVelocity < physics.restingVerticalSpeed) {
      current.verticalVelocity = 0.0;
      return withPitchBoundary(ball, rolled(current, physics, std::max(remaining, 0.0)), pitch);
    }
  }
  // A ball still hopping after this many bounces in one tick hops no higher
  // than rounding; it stays down.
  current.height = 0.0;
  current.verticalVelocity = 0.0;
  return withPitchBoundary(ball, rolled(current, physics, std::max(remaining, 0.0)), pitch);
}

}  // namespace

void validate(const BallPhysics& physics) {
  const bool valid =
      isPositiveFinite(physics.rollingDeceleration) && isPositiveFinite(physics.gravity) &&
      isFiniteNonNegative(physics.carryDistance) && isFiniteNonNegative(physics.airDrag) &&
      isFiniteNonNegative(physics.spinDecay) && isFiniteNonNegative(physics.restingVerticalSpeed) &&
      isShare(physics.bounceRestitution) && isShare(physics.bounceGrip) &&
      isShare(physics.spinTransfer);
  if (!valid) {
    throw std::invalid_argument(
        "ball physics: rolling deceleration and gravity must be positive and finite, carry "
        "distance, air drag, spin decay and resting vertical speed finite and not negative, and "
        "bounce restitution, bounce grip and spin transfer between 0 and 1");
  }
}

bool isInFlight(const BallState& ball) noexcept {
  return ball.height > 0.0 || ball.verticalVelocity != 0.0;
}

double rollingDistance(const double speed, const BallPhysics& physics) noexcept {
  return (speed * speed) / (2.0 * physics.rollingDeceleration);
}

double pitchExitFraction(const Vec2 start, const Vec2 end, const Pitch& pitch) noexcept {
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

std::optional<BallLanding> predictBallLanding(const BallState& ball,
                                              const BallPhysics& physics) noexcept {
  if (!isInFlight(ball)) {
    return std::nullopt;
  }
  // Bracket the landing first: a falling ball covers at least its terminal
  // speed per second, so doubling reaches the ground in a few steps.
  double horizon = 1.0;
  for (int step = 0; step < kMaxHorizonDoublings; ++step) {
    if (flownFor(ball, physics, horizon).height <= 0.0) {
      break;
    }
    horizon *= 2.0;
  }
  if (flownFor(ball, physics, horizon).height > 0.0) {
    // A ball without gravity never comes down.
    return std::nullopt;
  }
  const double seconds = landingSeconds(ball, physics, horizon);

  // The apex is where the ball stops rising. Its vertical velocity falls
  // throughout the flight, so the same bisection finds that moment.
  double low = 0.0;
  double high = seconds;
  for (int step = 0; step < kLandingIterations; ++step) {
    const double middle = low + ((high - low) / 2.0);
    if (flownFor(ball, physics, middle).verticalVelocity > 0.0) {
      low = middle;
    } else {
      high = middle;
    }
  }
  return BallLanding{.position = flownFor(ball, physics, seconds).position,
                     .seconds = seconds,
                     .apexHeight = std::max(flownFor(ball, physics, high).height, ball.height)};
}

BallState stepFreeBall(const BallState& ball, const BallPhysics& physics, const Pitch& pitch,
                       const double secondsPerTick) noexcept {
  if (isInFlight(ball)) {
    return flightStep(ball, physics, pitch, secondsPerTick);
  }
  return withPitchBoundary(ball, rolled(ball, physics, secondsPerTick), pitch);
}

}  // namespace ElyverseFootball::SimMatch
