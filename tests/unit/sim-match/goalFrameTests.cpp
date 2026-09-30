#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <optional>
#include <stdexcept>

#include "ballPhysics.hpp"
#include "goalFrame.hpp"
#include "matchState.hpp"
#include "pitch.hpp"
#include "vec2.hpp"

using Catch::Matchers::WithinAbs;
using ElyverseFootball::SimCore::Vec2;
using ElyverseFootball::SimMatch::BallPhysics;
using ElyverseFootball::SimMatch::BallState;
using ElyverseFootball::SimMatch::findWoodworkHit;
using ElyverseFootball::SimMatch::Goal;
using ElyverseFootball::SimMatch::GoalEnd;
using ElyverseFootball::SimMatch::GoalLineCrossing;
using ElyverseFootball::SimMatch::kBallRadius;
using ElyverseFootball::SimMatch::Pitch;
using ElyverseFootball::SimMatch::predictGoalLineCrossing;
using ElyverseFootball::SimMatch::WoodworkConfig;
using ElyverseFootball::SimMatch::WoodworkHit;
using ElyverseFootball::SimMatch::WoodworkPart;
using ElyverseFootball::SimMatch::woodworkPartName;

namespace {

// Long enough for a ball a meter from the goal line to get there.
constexpr double kSeconds = 0.1;

[[nodiscard]] const Pitch& pitch() {
  static const Pitch kPitch(60.0, 40.0);
  return kPitch;
}

[[nodiscard]] Goal goal() {
  return pitch().goal(GoalEnd::kMaxX);
}

// The ball's third dimension.
struct Flight {
  double height = 0.0;
  double verticalVelocity = 0.0;
};

[[nodiscard]] BallState ballAt(const Vec2 position, const Vec2 velocity, const Flight flight = {}) {
  BallState ball{
      .position = position, .velocity = velocity, .owner = std::nullopt, .lastTouch = std::nullopt};
  ball.height = flight.height;
  ball.verticalVelocity = flight.verticalVelocity;
  return ball;
}

[[nodiscard]] std::optional<WoodworkHit> hitOf(const BallState& ball,
                                               const WoodworkConfig& config = {}) {
  return findWoodworkHit(ball, BallPhysics{}, pitch(), config, kSeconds);
}

// A value to read a checked optional hit through.
[[nodiscard]] WoodworkHit checked(const std::optional<WoodworkHit>& hit) {
  REQUIRE(hit.has_value());
  return hit.value_or(WoodworkHit{});
}

}  // namespace

TEST_CASE("A ball rolled straight at a post comes straight back", "[goalFrame]") {
  const Vec2 post = goal().postAtMaxY();
  const WoodworkHit hit =
      checked(hitOf(ballAt(post - Vec2{.x = 1.0, .y = 0.0}, {.x = 20.0, .y = 0.0})));

  REQUIRE(hit.end == GoalEnd::kMaxX);
  REQUIRE(hit.part == WoodworkPart::kPostAtMaxY);
  // The ball's surface meets the post's: their radii apart.
  REQUIRE_THAT(hit.ball.position.x,
               WithinAbs(post.x - kBallRadius - WoodworkConfig{}.radius, 1e-9));
  REQUIRE(hit.seconds > 0.0);
  REQUIRE(hit.seconds < kSeconds);
  REQUIRE(hit.ball.velocity.x > 0.0);
  REQUIRE_THAT(hit.rebound.velocity.x,
               WithinAbs(-WoodworkConfig{}.restitution * hit.ball.velocity.x, 1e-9));
  REQUIRE_THAT(hit.rebound.velocity.y, WithinAbs(0.0, 1e-9));
  REQUIRE(hit.rebound.position == hit.ball.position);
}

TEST_CASE("A ball glancing off the inside of a post is turned into the goal", "[goalFrame]") {
  const Vec2 post = goal().postAtMinY();
  // A ball's width inside the post: it clips it on its way past.
  const WoodworkHit hit =
      checked(hitOf(ballAt(post + Vec2{.x = -1.0, .y = 0.12}, {.x = 20.0, .y = 0.0})));

  REQUIRE(hit.part == WoodworkPart::kPostAtMinY);
  REQUIRE(hit.rebound.velocity.y > 0.0);
  REQUIRE(hit.rebound.velocity.length() < hit.ball.velocity.length());
}

TEST_CASE("A ball passing clear of the posts touches nothing", "[goalFrame]") {
  const Goal frame = goal();
  const double clear = kBallRadius + WoodworkConfig{}.radius + 0.01;

  REQUIRE_FALSE(hitOf(ballAt(frame.center - Vec2{.x = 1.0, .y = 0.0}, {.x = 20.0, .y = 0.0})));
  REQUIRE_FALSE(
      hitOf(ballAt(frame.postAtMaxY() + Vec2{.x = -1.0, .y = clear}, {.x = 20.0, .y = 0.0})));
  REQUIRE_FALSE(
      hitOf(ballAt(frame.postAtMaxY() + Vec2{.x = -1.0, .y = -clear}, {.x = 20.0, .y = 0.0})));
  // Too slow to get there within the span.
  REQUIRE_FALSE(hitOf(ballAt(frame.postAtMaxY() - Vec2{.x = 1.0, .y = 0.0}, {.x = 2.0, .y = 0.0})));
  // At the other end of the pitch.
  REQUIRE_FALSE(hitOf(ballAt({.x = 30.0, .y = 20.0}, {.x = 20.0, .y = 0.0})));
}

TEST_CASE("A ball moving away from the frame it touches does not hit it", "[goalFrame]") {
  const Vec2 post = goal().postAtMaxY();
  const Vec2 touching = post - Vec2{.x = kBallRadius + WoodworkConfig{}.radius, .y = 0.0};

  REQUIRE_FALSE(hitOf(ballAt(touching, {.x = -10.0, .y = 0.0})));
  REQUIRE(hitOf(ballAt(touching, {.x = 10.0, .y = 0.0})).has_value());
}

TEST_CASE("A ball flying over the top of the frame misses the post", "[goalFrame]") {
  const Goal frame = goal();
  const double above = frame.heightMeters + (2.0 * WoodworkConfig{}.radius) + 0.5;

  REQUIRE_FALSE(hitOf(ballAt(frame.postAtMaxY() - Vec2{.x = 1.0, .y = 0.0}, {.x = 20.0, .y = 0.0},
                             {.height = above, .verticalVelocity = 1.0})));
}

TEST_CASE("A ball flying at the crossbar comes back off it", "[goalFrame]") {
  const Goal frame = goal();
  // Level with the bar's axis, so it meets it head on.
  const double level = frame.heightMeters + WoodworkConfig{}.radius - kBallRadius;
  // Gravity pulls a flat ball under the bar's axis on the way: it goes down.
  const WoodworkHit hit =
      checked(hitOf(ballAt(frame.center - Vec2{.x = 1.0, .y = 0.0}, {.x = 20.0, .y = 0.0},
                           {.height = level, .verticalVelocity = 0.25})));

  REQUIRE(hit.part == WoodworkPart::kCrossbar);
  REQUIRE(hit.end == GoalEnd::kMaxX);
  REQUIRE(hit.ball.position.x < frame.center.x);
  REQUIRE(hit.rebound.velocity.x < 0.0);
  REQUIRE_THAT(hit.rebound.velocity.y, WithinAbs(0.0, 1e-12));
  REQUIRE(hit.rebound.spin == 0.0);
}

TEST_CASE("A ball clipping the underside of the crossbar goes down", "[goalFrame]") {
  const Goal frame = goal();
  // Its top a few centimeters into the bar.
  const double height = frame.heightMeters - (2.0 * kBallRadius) + 0.05;
  const WoodworkHit hit =
      checked(hitOf(ballAt(frame.center - Vec2{.x = 1.0, .y = 0.0}, {.x = 20.0, .y = 0.0},
                           {.height = height, .verticalVelocity = 0.25})));

  REQUIRE(hit.part == WoodworkPart::kCrossbar);
  REQUIRE(hit.rebound.verticalVelocity < hit.ball.verticalVelocity);
  // Still on its way in.
  REQUIRE(hit.rebound.velocity.x > 0.0);
}

TEST_CASE("A ball under or over the crossbar, or beside the goal, misses it", "[goalFrame]") {
  const Goal frame = goal();
  const Vec2 before = frame.center - Vec2{.x = 1.0, .y = 0.0};
  const Vec2 velocity{.x = 20.0, .y = 0.0};

  REQUIRE_FALSE(hitOf(
      ballAt(before, velocity, {.height = frame.heightMeters - 0.5, .verticalVelocity = 0.25})));
  REQUIRE_FALSE(hitOf(
      ballAt(before, velocity, {.height = frame.heightMeters + 0.5, .verticalVelocity = 0.25})));
  const double level = frame.heightMeters + WoodworkConfig{}.radius - kBallRadius;
  REQUIRE_FALSE(hitOf(ballAt(before + Vec2{.x = 0.0, .y = frame.widthMeters}, velocity,
                             {.height = level, .verticalVelocity = 0.25})));
}

TEST_CASE("The frame at the other end is hit the same way", "[goalFrame]") {
  const Vec2 post = pitch().goal(GoalEnd::kMinX).postAtMinY();
  const WoodworkHit hit =
      checked(hitOf(ballAt(post + Vec2{.x = 1.0, .y = 0.0}, {.x = -20.0, .y = 0.0})));

  REQUIRE(hit.end == GoalEnd::kMinX);
  REQUIRE(hit.part == WoodworkPart::kPostAtMinY);
  REQUIRE(hit.rebound.velocity.x > 0.0);
}

TEST_CASE("The restitution sets how hard a ball comes off the frame", "[goalFrame]") {
  const BallState ball =
      ballAt(goal().postAtMaxY() - Vec2{.x = 1.0, .y = 0.0}, {.x = 20.0, .y = 0.0});

  REQUIRE_THAT(checked(hitOf(ball, {.radius = 0.06, .restitution = 0.0})).rebound.velocity.x,
               WithinAbs(0.0, 1e-9));
  const WoodworkHit elastic = checked(hitOf(ball, {.radius = 0.06, .restitution = 1.0}));
  REQUIRE_THAT(elastic.rebound.velocity.x, WithinAbs(-elastic.ball.velocity.x, 1e-9));
}

TEST_CASE("An invalid woodwork configuration is rejected", "[goalFrame]") {
  REQUIRE_NOTHROW(validate(WoodworkConfig{}));
  REQUIRE_THROWS_AS(validate(WoodworkConfig{.radius = 0.0, .restitution = 0.5}),
                    std::invalid_argument);
  REQUIRE_THROWS_AS(validate(WoodworkConfig{.radius = 0.06, .restitution = 1.5}),
                    std::invalid_argument);
  REQUIRE_THROWS_AS(validate(WoodworkConfig{.radius = std::nan(""), .restitution = 0.5}),
                    std::invalid_argument);
}

TEST_CASE("The parts of the frame have names", "[goalFrame]") {
  REQUIRE(woodworkPartName(WoodworkPart::kPostAtMinY) == "postAtMinY");
  REQUIRE(woodworkPartName(WoodworkPart::kPostAtMaxY) == "postAtMaxY");
  REQUIRE(woodworkPartName(WoodworkPart::kCrossbar) == "crossbar");
}

TEST_CASE("A ball's crossing of the goal line is predicted from its flight", "[goalFrame]") {
  // Rolling diagonally: a meter across for every two along.
  const auto rolling =
      predictGoalLineCrossing(ballAt({.x = 50.0, .y = 18.0}, {.x = 20.0, .y = 10.0}), BallPhysics{},
                              pitch(), GoalEnd::kMaxX);
  REQUIRE(rolling.has_value());
  REQUIRE_THAT(rolling.value_or(GoalLineCrossing{}).y, WithinAbs(23.0, 1e-9));
  REQUIRE(rolling.value_or(GoalLineCrossing{}).height == 0.0);

  // In the air it is still above the ground when it gets there.
  const auto flying = predictGoalLineCrossing(ballAt({.x = 50.0, .y = 20.0}, {.x = 25.0, .y = 0.0},
                                                     {.height = 0.0, .verticalVelocity = 4.0}),
                                              BallPhysics{}, pitch(), GoalEnd::kMaxX);
  REQUIRE(flying.has_value());
  REQUIRE(flying.value_or(GoalLineCrossing{}).height > 0.5);

  // On slow grass a ball is followed for as long as it rolls: this one takes
  // more than eleven seconds over its ten meters.
  const auto slow =
      predictGoalLineCrossing(ballAt({.x = 50.0, .y = 20.0}, {.x = 1.45, .y = 0.0}),
                              BallPhysics{.rollingDeceleration = 0.1}, pitch(), GoalEnd::kMaxX);
  REQUIRE(slow.has_value());
  REQUIRE_THAT(slow.value_or(GoalLineCrossing{}).y, WithinAbs(20.0, 1e-9));
}

TEST_CASE("A ball that never reaches the goal line crosses nowhere", "[goalFrame]") {
  const BallPhysics physics;
  // Moving away, at rest, and too slow to roll ten meters.
  REQUIRE_FALSE(predictGoalLineCrossing(ballAt({.x = 50.0, .y = 20.0}, {.x = -20.0, .y = 0.0}),
                                        physics, pitch(), GoalEnd::kMaxX));
  REQUIRE_FALSE(predictGoalLineCrossing(ballAt({.x = 50.0, .y = 20.0}, {}), physics, pitch(),
                                        GoalEnd::kMaxX));
  REQUIRE_FALSE(predictGoalLineCrossing(ballAt({.x = 50.0, .y = 20.0}, {.x = 3.0, .y = 0.0}),
                                        physics, pitch(), GoalEnd::kMaxX));
  REQUIRE(predictGoalLineCrossing(ballAt({.x = 50.0, .y = 20.0}, {.x = -20.0, .y = 0.0}), physics,
                                  pitch(), GoalEnd::kMinX)
              .has_value());
}
