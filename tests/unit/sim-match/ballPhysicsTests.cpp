#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <limits>
#include <optional>
#include <stdexcept>
#include <vector>

#include "ballPhysics.hpp"
#include "matchState.hpp"
#include "pitch.hpp"
#include "vec2.hpp"

using Catch::Matchers::WithinAbs;
using ElyverseFootball::SimCore::Vec2;
using ElyverseFootball::SimMatch::BallLanding;
using ElyverseFootball::SimMatch::BallPhysics;
using ElyverseFootball::SimMatch::BallState;
using ElyverseFootball::SimMatch::isInFlight;
using ElyverseFootball::SimMatch::Pitch;
using ElyverseFootball::SimMatch::predictBallLanding;
using ElyverseFootball::SimMatch::stepFreeBall;
using ElyverseFootball::SimMatch::validate;

namespace {

constexpr double kSecondsPerTick = 1.0 / 30.0;

// Built on first use, not while static objects are initialized: Pitch's
// constructor validates its dimensions and may throw.
[[nodiscard]] const Pitch& pitch() {
  static const Pitch kPitch(60.0, 40.0);
  return kPitch;
}

// The third dimension of a ball under test, named at the call site so no two
// of its three numbers can be swapped.
struct Flight {
  double height = 0.0;
  double verticalVelocity = 0.0;
  double spin = 0.0;
};

[[nodiscard]] BallState ballAt(const Vec2 position, const Vec2 velocity = {},
                               const Flight flight = {}) {
  BallState ball;
  ball.position = position;
  ball.velocity = velocity;
  ball.height = flight.height;
  ball.verticalVelocity = flight.verticalVelocity;
  ball.spin = flight.spin;
  return ball;
}

// The ball after this many seconds, stepped at this tick rate.
[[nodiscard]] BallState after(const BallState& ball, const double seconds,
                              const double secondsPerTick, const BallPhysics physics = {}) {
  BallState moved = ball;
  const auto ticks = static_cast<int>(std::lround(seconds / secondsPerTick));
  for (int tick = 0; tick < ticks; ++tick) {
    moved = stepFreeBall(moved, physics, pitch(), secondsPerTick);
  }
  return moved;
}

// Every height the ball reaches, tick by tick, until it stops flying.
[[nodiscard]] std::vector<double> heights(const BallState& ball, const int ticks,
                                          const BallPhysics physics = {}) {
  std::vector<double> reached;
  BallState moved = ball;
  for (int tick = 0; tick < ticks; ++tick) {
    moved = stepFreeBall(moved, physics, pitch(), kSecondsPerTick);
    reached.push_back(moved.height);
  }
  return reached;
}

// The apex of every hop: each local maximum of a sequence of heights.
[[nodiscard]] std::vector<double> apexes(const std::vector<double>& reached) {
  std::vector<double> found;
  for (std::size_t index = 1; index + 1 < reached.size(); ++index) {
    if (reached[index] > reached[index - 1] && reached[index] >= reached[index + 1]) {
      found.push_back(reached[index]);
    }
  }
  return found;
}

}  // namespace

TEST_CASE("A ball on the ground rolls exactly as it always has", "[ballPhysics]") {
  const BallPhysics physics;
  const BallState ball = ballAt({.x = 10.0, .y = 20.0}, {.x = 9.0, .y = 0.0});
  REQUIRE_FALSE(isInFlight(ball));

  // Constant deceleration, exactly integrated: after one tick the ball has
  // covered the average of its start and end speed, and it stops after its
  // rolling distance.
  const BallState stepped = stepFreeBall(ball, physics, pitch(), kSecondsPerTick);
  const double endSpeed = 9.0 - (physics.rollingDeceleration * kSecondsPerTick);
  REQUIRE_THAT(stepped.position.x,
               WithinAbs(10.0 + ((9.0 + endSpeed) / 2.0 * kSecondsPerTick), 1e-12));
  REQUIRE_THAT(stepped.velocity.x, WithinAbs(endSpeed, 1e-12));
  REQUIRE(stepped.height == 0.0);
  REQUIRE(stepped.verticalVelocity == 0.0);

  const BallState stopped = after(ball, 10.0, kSecondsPerTick);
  REQUIRE(stopped.velocity == Vec2{});
  REQUIRE_THAT(stopped.position.x, WithinAbs(10.0 + ((9.0 * 9.0) / (2.0 * 1.5)), 1e-9));
  REQUIRE(stopped.isAtRest());
}

TEST_CASE("A ball lifted off the grass is in flight before it has any height", "[ballPhysics]") {
  REQUIRE(isInFlight(ballAt({.x = 30.0, .y = 20.0}, {}, {.verticalVelocity = 5.0})));
  REQUIRE(isInFlight(ballAt({.x = 30.0, .y = 20.0}, {}, {.height = 2.0})));
  REQUIRE_FALSE(isInFlight(ballAt({.x = 30.0, .y = 20.0}, {.x = 8.0, .y = 0.0})));
}

TEST_CASE("Without drag a flight is the plain parabola", "[ballPhysics]") {
  // The one case with an answer to check against that the model did not
  // produce itself.
  BallPhysics physics;
  physics.airDrag = 0.0;
  physics.spinDecay = 0.0;
  const BallState ball =
      ballAt({.x = 10.0, .y = 20.0}, {.x = 12.0, .y = 0.0}, {.verticalVelocity = 6.0});

  const BallState flying = after(ball, 0.5, kSecondsPerTick, physics);
  REQUIRE_THAT(flying.position.x, WithinAbs(10.0 + (12.0 * 0.5), 1e-9));
  REQUIRE_THAT(flying.height, WithinAbs((6.0 * 0.5) - (0.5 * physics.gravity * 0.25), 1e-9));
  REQUIRE_THAT(flying.verticalVelocity, WithinAbs(6.0 - (physics.gravity * 0.5), 1e-9));

  // It comes down after 2 v / g, having kept every bit of its speed.
  const auto landing = predictBallLanding(ball, physics);
  REQUIRE(landing.has_value());
  REQUIRE_THAT(landing.value_or(BallLanding{}).seconds,
               WithinAbs(2.0 * 6.0 / physics.gravity, 1e-9));
  REQUIRE_THAT(landing.value_or(BallLanding{}).apexHeight,
               WithinAbs((6.0 * 6.0) / (2.0 * physics.gravity), 1e-9));
}

TEST_CASE("A vanishing drag flies like no drag at all", "[ballPhysics]") {
  // validate() accepts every finite drag down to zero, so every one of them
  // has to fly: the terminal speed gravity / airDrag overflows long before
  // the smallest, and 1 - e^{-k·t} cancels to nothing not far above it.
  BallPhysics none;
  none.airDrag = 0.0;
  const BallState ball =
      ballAt({.x = 10.0, .y = 20.0}, {.x = 12.0, .y = 0.0}, {.verticalVelocity = 6.0});
  const BallState parabola = stepFreeBall(ball, none, pitch(), kSecondsPerTick);

  for (const double drag : {1e-300, std::numeric_limits<double>::min(), 1e-10}) {
    BallPhysics physics = none;
    physics.airDrag = drag;
    const BallState flown = stepFreeBall(ball, physics, pitch(), kSecondsPerTick);
    // None of these drags takes as much as a nanometer off a tick, so the
    // flight has to be the parabola to within one.
    REQUIRE(std::isfinite(flown.height));
    REQUIRE_THAT(flown.height, WithinAbs(parabola.height, 1e-9));
    REQUIRE_THAT(flown.position.x, WithinAbs(parabola.position.x, 1e-9));
    REQUIRE_THAT(flown.verticalVelocity, WithinAbs(parabola.verticalVelocity, 1e-9));
  }
}

TEST_CASE("Air drag shortens a flight and caps a fall", "[ballPhysics]") {
  const BallPhysics physics;
  BallPhysics without = physics;
  without.airDrag = 0.0;
  const BallState ball =
      ballAt({.x = 5.0, .y = 20.0}, {.x = 18.0, .y = 0.0}, {.verticalVelocity = 8.0});

  const auto dragged = predictBallLanding(ball, physics);
  const auto free = predictBallLanding(ball, without);
  REQUIRE(dragged.has_value());
  REQUIRE(free.has_value());
  const BallLanding withDrag = dragged.value_or(BallLanding{});
  const BallLanding withoutDrag = free.value_or(BallLanding{});
  REQUIRE(withDrag.position.x < withoutDrag.position.x);
  REQUIRE(withDrag.apexHeight < withoutDrag.apexHeight);
  REQUIRE(withDrag.seconds < withoutDrag.seconds);

  // A long fall approaches the terminal speed gravity / airDrag and goes no
  // faster. Half a minute from two kilometers up is still a fall.
  const BallState falling =
      after(ballAt({.x = 30.0, .y = 20.0}, {}, {.height = 2000.0}), 30.0, kSecondsPerTick);
  const double terminal = physics.gravity / physics.airDrag;
  REQUIRE(falling.height > 0.0);
  REQUIRE(falling.verticalVelocity >= -terminal);
  REQUIRE_THAT(falling.verticalVelocity, WithinAbs(-terminal, 0.01));
}

TEST_CASE("A bounce loses energy until the ball rolls", "[ballPhysics]") {
  const BallState dropped = ballAt({.x = 30.0, .y = 20.0}, {.x = 4.0, .y = 0.0}, {.height = 3.0});

  const std::vector<double> reached = heights(dropped, 300);
  const std::vector<double> hops = apexes(reached);
  REQUIRE(hops.size() >= 2);
  for (std::size_t index = 1; index < hops.size(); ++index) {
    CAPTURE(index);
    REQUIRE(hops[index] < hops[index - 1]);
  }
  // Never below the grass, and never higher than it was dropped from.
  for (const double height : reached) {
    REQUIRE(height >= 0.0);
    REQUIRE(height <= 3.0);
  }

  // Ten seconds on it lies still on the ground, having rolled forward.
  const BallState settled = after(dropped, 10.0, kSecondsPerTick);
  REQUIRE(settled.isAtRest());
  REQUIRE(settled.position.x > 30.0);
}

TEST_CASE("A ball that does not bounce settles on its one impact", "[ballPhysics]") {
  // A dead surface: nothing comes back up, and nothing is slow enough to be
  // called resting either. The landing must still cost the ball one helping of
  // grip and of spin, not the eight a tick has room for.
  BallPhysics dead;
  dead.bounceRestitution = 0.0;
  dead.restingVerticalSpeed = 0.0;
  const BallState dropped = ballAt({.x = 30.0, .y = 20.0}, {.x = 6.0, .y = 0.0},
                                   {.height = 0.01, .verticalVelocity = -2.0, .spin = 20.0});

  const BallState landed = stepFreeBall(dropped, dead, pitch(), kSecondsPerTick);

  REQUIRE(landed.height == 0.0);
  REQUIRE(landed.verticalVelocity == 0.0);
  // One bounce spends spinTransfer of the spin and keeps bounceGrip of the
  // speed; eight would leave 0.8^8 of each, a fifth of that.
  REQUIRE_THAT(landed.spin, WithinAbs(20.0 * (1.0 - dead.spinTransfer), 0.1));
  REQUIRE(landed.velocity.x > 6.0 * dead.bounceGrip);
}

TEST_CASE("A flight comes out the same at any tick rate", "[ballPhysics]") {
  const BallState ball =
      ballAt({.x = 8.0, .y = 20.0}, {.x = 14.0, .y = 3.0}, {.verticalVelocity = 9.0, .spin = 30.0});

  // Two seconds of flight and a bounce, cut into ticks of very different
  // length: the bounce is resolved at its own moment, not at a tick boundary.
  const BallState slow = after(ball, 2.0, 1.0 / 30.0);
  const BallState fast = after(ball, 2.0, 1.0 / 120.0);
  const BallState faster = after(ball, 2.0, 1.0 / 300.0);

  for (const BallState& stepped : {fast, faster}) {
    REQUIRE_THAT(stepped.position.x, WithinAbs(slow.position.x, 1e-6));
    REQUIRE_THAT(stepped.position.y, WithinAbs(slow.position.y, 1e-6));
    REQUIRE_THAT(stepped.height, WithinAbs(slow.height, 1e-6));
    REQUIRE_THAT(stepped.verticalVelocity, WithinAbs(slow.verticalVelocity, 1e-6));
    REQUIRE_THAT(stepped.spin, WithinAbs(slow.spin, 1e-6));
  }
}

TEST_CASE("The landing prediction matches the flight it predicts", "[ballPhysics]") {
  const BallPhysics physics;
  const BallState ball = ballAt({.x = 12.0, .y = 18.0}, {.x = 11.0, .y = -2.0},
                                {.height = 1.0, .verticalVelocity = 7.0});

  const auto landing = predictBallLanding(ball, physics);
  REQUIRE(landing.has_value());
  const BallLanding predicted = landing.value_or(BallLanding{});

  // Stepped to the predicted moment, the ball is where the prediction said,
  // and it is still in the air one tick earlier.
  const BallState arrived = after(ball, predicted.seconds, predicted.seconds / 600.0, physics);
  REQUIRE_THAT(arrived.position.x, WithinAbs(predicted.position.x, 1e-3));
  REQUIRE_THAT(arrived.position.y, WithinAbs(predicted.position.y, 1e-3));
  REQUIRE(predicted.apexHeight > ball.height);

  // A ball on the grass has no landing to predict.
  REQUIRE_FALSE(predictBallLanding(ballAt({.x = 30.0, .y = 20.0}, {.x = 5.0, .y = 0.0}), physics)
                    .has_value());
}

TEST_CASE("Spin drives a bounce on and dies away in flight", "[ballPhysics]") {
  const BallPhysics physics;
  const auto speedAfterBounce = [&physics](const double spin) {
    const BallState ball =
        ballAt({.x = 30.0, .y = 20.0}, {.x = 10.0, .y = 0.0}, {.height = 2.0, .spin = spin});
    return after(ball, 1.5, kSecondsPerTick, physics).velocity.x;
  };

  REQUIRE(speedAfterBounce(60.0) > speedAfterBounce(0.0));
  REQUIRE(speedAfterBounce(-60.0) < speedAfterBounce(0.0));
  // Backspin checks the ball; it never drives it backwards.
  REQUIRE(speedAfterBounce(-400.0) >= 0.0);

  // In the air spin fades towards zero without changing sign.
  const BallState spinning =
      ballAt({.x = 30.0, .y = 20.0}, {.x = 6.0, .y = 0.0}, {.height = 5.0, .spin = 50.0});
  const BallState later = after(spinning, 0.5, kSecondsPerTick, physics);
  REQUIRE(later.spin > 0.0);
  REQUIRE(later.spin < 50.0);
}

TEST_CASE("A ball that flies over the line stops on it", "[ballPhysics]") {
  // Heading for the touchline at y = 40 with two meters of height left.
  const BallState ball = ballAt({.x = 30.0, .y = 39.0}, {.x = 0.0, .y = 12.0},
                                {.height = 2.0, .verticalVelocity = 1.0, .spin = 20.0});

  const BallState stepped = after(ball, 0.5, kSecondsPerTick);

  REQUIRE_THAT(stepped.position.y, WithinAbs(40.0, 1e-9));
  REQUIRE(stepped.isAtRest());
  REQUIRE(stepped.spin == 0.0);
}

TEST_CASE("A ball asked where it will be follows the flight it is stepped on", "[ballPhysics]") {
  using ElyverseFootball::SimMatch::ballAfter;
  const BallPhysics physics;
  // A shot that bounces on the way, and a ball rolling to a stop.
  const BallState flying = ballAt({.x = 10.0, .y = 20.0}, {.x = 12.0, .y = 3.0},
                                  {.height = 0.0, .verticalVelocity = 4.0, .spin = 10.0});
  const BallState rolling = ballAt({.x = 10.0, .y = 20.0}, {.x = 3.0, .y = 0.0});

  for (const BallState& ball : {flying, rolling}) {
    const BallState stepped = stepFreeBall(ball, physics, pitch(), 0.9);
    const BallState asked = ballAfter(ball, physics, 0.9);
    REQUIRE(asked == stepped);
  }
  REQUIRE(ballAfter(flying, physics, 0.0) == flying);
  // It knows no pitch boundary.
  const BallState leaving = ballAt({.x = 55.0, .y = 20.0}, {.x = 20.0, .y = 0.0});
  REQUIRE(stepFreeBall(leaving, physics, pitch(), 1.0).position.x == 60.0);
  REQUIRE(ballAfter(leaving, physics, 1.0).position.x > 70.0);
}

TEST_CASE("A launch is solved for the height the ball should arrive at", "[ballPhysics]") {
  using ElyverseFootball::SimMatch::ballAfter;
  using ElyverseFootball::SimMatch::launchVerticalVelocity;
  for (const BallPhysics& physics : {BallPhysics{}, BallPhysics{.airDrag = 0.0}}) {
    for (const double height : {0.0, 0.5, 2.4}) {
      CAPTURE(physics.airDrag, height);
      const auto lift =
          launchVerticalVelocity({.speed = 25.0, .distance = 16.0, .height = height}, physics);
      REQUIRE(lift.has_value());
      REQUIRE(lift.value_or(0.0) > 0.0);

      // Follow it until it has come that far along the ground.
      const BallState ball = ballAt({.x = 10.0, .y = 20.0}, {.x = 25.0, .y = 0.0},
                                    {.height = 0.0, .verticalVelocity = lift.value_or(0.0)});
      double low = 0.0;
      double high = 2.0;
      for (int step = 0; step < 64; ++step) {
        const double middle = (low + high) / 2.0;
        (ballAfter(ball, physics, middle).position.x < 26.0 ? low : high) = middle;
      }
      REQUIRE_THAT(ballAfter(ball, physics, high).height, WithinAbs(height, 1e-9));
      // It rises and falls once, without touching the ground on the way.
      REQUIRE(ballAfter(ball, physics, high / 2.0).height > height / 2.0);
    }
  }
}

TEST_CASE("A ball cannot be launched farther than drag lets it fly", "[ballPhysics]") {
  using ElyverseFootball::SimMatch::launchVerticalVelocity;
  // 25 m/s against a drag of 0.33 per second never gets past 25 / 0.33 m.
  const auto launch = [](const double speed, const double distance) {
    return launchVerticalVelocity({.speed = speed, .distance = distance, .height = 1.0},
                                  BallPhysics{});
  };
  REQUIRE(launch(25.0, 75.0).has_value());
  REQUIRE_FALSE(launch(25.0, 76.0).has_value());
  REQUIRE_FALSE(launch(0.0, 10.0).has_value());
  REQUIRE_FALSE(launch(25.0, 0.0).has_value());
}

TEST_CASE("The physics reject constants a ball cannot have", "[ballPhysics]") {
  REQUIRE_NOTHROW(validate(BallPhysics{}));

  const auto rejects = [](auto change) {
    BallPhysics physics;
    change(physics);
    REQUIRE_THROWS_AS(validate(physics), std::invalid_argument);
  };
  rejects([](BallPhysics& physics) { physics.rollingDeceleration = 0.0; });
  rejects([](BallPhysics& physics) { physics.gravity = -9.81; });
  rejects([](BallPhysics& physics) { physics.carryDistance = -0.1; });
  rejects([](BallPhysics& physics) { physics.airDrag = -1.0; });
  rejects([](BallPhysics& physics) { physics.bounceRestitution = 1.5; });
  rejects([](BallPhysics& physics) { physics.bounceGrip = -0.1; });
  rejects([](BallPhysics& physics) { physics.spinTransfer = 2.0; });
  rejects([](BallPhysics& physics) { physics.spinDecay = -0.5; });
  rejects([](BallPhysics& physics) { physics.restingVerticalSpeed = -1.0; });
}
