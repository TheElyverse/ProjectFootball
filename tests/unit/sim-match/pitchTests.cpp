#include <array>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <cstddef>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <vector>

#include "pitch.hpp"

using Catch::Matchers::WithinAbs;
using ElyverseFootball::SimCore::Vec2;
using ElyverseFootball::SimMatch::Goal;
using ElyverseFootball::SimMatch::GoalEnd;
using ElyverseFootball::SimMatch::kSandboxLengthMeters;
using ElyverseFootball::SimMatch::kSandboxWidthMeters;
using ElyverseFootball::SimMatch::kStandardLengthMeters;
using ElyverseFootball::SimMatch::kStandardMarkings;
using ElyverseFootball::SimMatch::kStandardWidthMeters;
using ElyverseFootball::SimMatch::Pitch;
using ElyverseFootball::SimMatch::PitchCircle;
using ElyverseFootball::SimMatch::PitchCorner;
using ElyverseFootball::SimMatch::PitchMarkings;
using ElyverseFootball::SimMatch::PitchRect;

TEST_CASE("Pitch preserves configurable dimensions in meters", "[pitch]") {
  const Pitch smallPitch(60.0, 40.0);
  const Pitch largePitch(105.0, 68.0);

  REQUIRE(smallPitch.lengthMeters() == 60.0);
  REQUIRE(smallPitch.widthMeters() == 40.0);
  REQUIRE(largePitch.lengthMeters() == 105.0);
  REQUIRE(largePitch.widthMeters() == 68.0);
  REQUIRE_FALSE(smallPitch.contains({.x = 80.0, .y = 50.0}));
  REQUIRE(largePitch.contains({.x = 80.0, .y = 50.0}));
}

TEST_CASE("Pitch includes its interior, edges and corners", "[pitch]") {
  const Pitch pitch(60.0, 40.0);
  constexpr std::array points{
      Vec2{.x = 30.0, .y = 20.0}, Vec2{.x = 0.0, .y = 0.0},   Vec2{.x = 60.0, .y = 0.0},
      Vec2{.x = 0.0, .y = 40.0},  Vec2{.x = 60.0, .y = 40.0}, Vec2{.x = 0.0, .y = 20.0},
      Vec2{.x = 60.0, .y = 20.0}, Vec2{.x = 30.0, .y = 0.0},  Vec2{.x = 30.0, .y = 40.0},
  };
  for (const Vec2 point : points) {
    CAPTURE(point.x, point.y);
    REQUIRE(pitch.contains(point));
  }
}

TEST_CASE("Pitch excludes points immediately outside each boundary", "[pitch]") {
  const Pitch pitch(60.0, 40.0);
  const std::array points{
      Vec2{.x = std::nextafter(0.0, -0.1), .y = 20.0},
      Vec2{.x = std::nextafter(60.0, 60.1), .y = 20.0},
      Vec2{.x = 30.0, .y = std::nextafter(0.0, -0.1)},
      Vec2{.x = 30.0, .y = std::nextafter(40.0, 40.1)},
  };
  for (const Vec2 point : points) {
    CAPTURE(point.x, point.y);
    REQUIRE_FALSE(pitch.contains(point));
  }
}

TEST_CASE("Pitch rejects nonpositive and nonfinite dimensions", "[pitch]") {
  const double dimension =
      GENERATE(0.0, -0.0, -1.0, std::numeric_limits<double>::lowest(),
               std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity(),
               -std::numeric_limits<double>::infinity());
  CAPTURE(dimension);
  REQUIRE_THROWS_AS(Pitch(dimension, 40.0), std::invalid_argument);
  REQUIRE_THROWS_AS(Pitch(60.0, dimension), std::invalid_argument);
}

TEST_CASE("Pitch excludes nonfinite positions", "[pitch]") {
  const Pitch pitch(60.0, 40.0);
  constexpr std::array invalidValues{
      std::numeric_limits<double>::quiet_NaN(),
      std::numeric_limits<double>::infinity(),
      -std::numeric_limits<double>::infinity(),
  };
  for (const double value : invalidValues) {
    CAPTURE(value);
    REQUIRE_FALSE(pitch.contains({.x = value, .y = 20.0}));
    REQUIRE_FALSE(pitch.contains({.x = 30.0, .y = value}));
  }
}

TEST_CASE("Pitch supports positive finite dimension extremes", "[pitch]") {
  constexpr std::array dimensions{
      std::numeric_limits<double>::denorm_min(),
      std::numeric_limits<double>::min(),
      std::numeric_limits<double>::max(),
  };
  for (const double dimension : dimensions) {
    CAPTURE(dimension);
    const Pitch pitch(dimension, dimension);
    REQUIRE(pitch.contains({.x = 0.0, .y = 0.0}));
    REQUIRE(pitch.contains({.x = dimension, .y = dimension}));
  }
}

TEST_CASE("Pitch compares both dimensions exactly", "[pitch]") {
  const Pitch pitch(60.0, 40.0);

  REQUIRE(pitch == Pitch(60.0, 40.0));
  REQUIRE_FALSE(pitch == Pitch(40.0, 60.0));
  REQUIRE_FALSE(pitch == Pitch(std::nextafter(60.0, 61.0), 40.0));
}

TEST_CASE("Pitch::clamp keeps points on the pitch and projects others onto it", "[pitch]") {
  const Pitch pitch(60.0, 40.0);

  REQUIRE(pitch.clamp({.x = 12.5, .y = 7.0}) == Vec2{.x = 12.5, .y = 7.0});
  REQUIRE(pitch.clamp({.x = 60.0, .y = 0.0}) == Vec2{.x = 60.0, .y = 0.0});
  REQUIRE(pitch.clamp({.x = -3.0, .y = 20.0}) == Vec2{.x = 0.0, .y = 20.0});
  REQUIRE(pitch.clamp({.x = 30.0, .y = 41.0}) == Vec2{.x = 30.0, .y = 40.0});
  REQUIRE(pitch.clamp({.x = 75.0, .y = -8.0}) == Vec2{.x = 60.0, .y = 0.0});
}

namespace {

constexpr double kTolerance = 1e-12;

// The sizes every derived marking has to survive: the two named defaults, legal
// extremes of the Laws' range, deliberately lopsided pitches, and the floating
// point extremes the constructor still accepts.
[[nodiscard]] std::vector<Pitch> pitchSizes() {
  return {
      Pitch(kStandardLengthMeters, kStandardWidthMeters),
      Pitch(kSandboxLengthMeters, kSandboxWidthMeters),
      Pitch(90.0, 45.0),
      Pitch(120.0, 90.0),
      Pitch(300.0, 50.0),
      Pitch(50.0, 300.0),
      Pitch(0.001, 0.001),
      Pitch(std::numeric_limits<double>::denorm_min(), std::numeric_limits<double>::denorm_min()),
      Pitch(std::numeric_limits<double>::max(), std::numeric_limits<double>::max()),
  };
}

[[nodiscard]] std::array<double, 9> markingValues(const PitchMarkings& markings) {
  return {markings.goalWidthMeters,           markings.goalHeightMeters,
          markings.penaltyAreaDepthMeters,    markings.penaltyAreaWidthMeters,
          markings.goalAreaDepthMeters,       markings.goalAreaWidthMeters,
          markings.penaltySpotDistanceMeters, markings.centerCircleRadiusMeters,
          markings.cornerArcRadiusMeters};
}

}  // namespace

TEST_CASE("Pitch markings are the Laws' own numbers at the standard size", "[pitch][markings]") {
  const Pitch pitch(kStandardLengthMeters, kStandardWidthMeters);

  REQUIRE(pitch.markings() == kStandardMarkings);
}

TEST_CASE("Pitch markings scale with a pitch of another size", "[pitch][markings]") {
  const Pitch pitch(kSandboxLengthMeters, kSandboxWidthMeters);
  const PitchMarkings markings = pitch.markings();
  const double depthScale = kSandboxLengthMeters / kStandardLengthMeters;
  const double widthScale = kSandboxWidthMeters / kStandardWidthMeters;

  // Depths follow the length, widths the width, radii the smaller of the two.
  REQUIRE_THAT(markings.goalWidthMeters, WithinAbs(7.32 * widthScale, kTolerance));
  REQUIRE_THAT(markings.goalHeightMeters, WithinAbs(2.44 * widthScale, kTolerance));
  REQUIRE_THAT(markings.penaltyAreaDepthMeters, WithinAbs(16.5 * depthScale, kTolerance));
  REQUIRE_THAT(markings.penaltyAreaWidthMeters, WithinAbs(40.32 * widthScale, kTolerance));
  REQUIRE_THAT(markings.goalAreaDepthMeters, WithinAbs(5.5 * depthScale, kTolerance));
  REQUIRE_THAT(markings.goalAreaWidthMeters, WithinAbs(18.32 * widthScale, kTolerance));
  REQUIRE_THAT(markings.penaltySpotDistanceMeters, WithinAbs(11.0 * depthScale, kTolerance));
  REQUIRE(depthScale < widthScale);
  REQUIRE_THAT(markings.centerCircleRadiusMeters, WithinAbs(9.15 * depthScale, kTolerance));
  REQUIRE_THAT(markings.cornerArcRadiusMeters, WithinAbs(1.0 * depthScale, kTolerance));
}

TEST_CASE("Pitch markings stay finite and fit inside any pitch", "[pitch][markings]") {
  for (const Pitch& pitch : pitchSizes()) {
    CAPTURE(pitch.lengthMeters(), pitch.widthMeters());
    const PitchMarkings markings = pitch.markings();
    for (const double value : markingValues(markings)) {
      CAPTURE(value);
      REQUIRE(std::isfinite(value));
      REQUIRE(value >= 0.0);
    }

    for (const GoalEnd end : {GoalEnd::kMinX, GoalEnd::kMaxX}) {
      const PitchRect penaltyArea = pitch.penaltyArea(end);
      const PitchRect goalArea = pitch.goalArea(end);
      const Goal goal = pitch.goal(end);
      REQUIRE(pitch.contains(penaltyArea.min));
      REQUIRE(pitch.contains(penaltyArea.max));
      REQUIRE(penaltyArea.contains(goalArea.min));
      REQUIRE(penaltyArea.contains(goalArea.max));
      REQUIRE(penaltyArea.contains(pitch.penaltySpot(end)));
      REQUIRE(goalArea.contains(goal.postAtMinY()));
      REQUIRE(goalArea.contains(goal.postAtMaxY()));
      REQUIRE(pitch.cornerArc(PitchCorner::kMinXMinY).radiusMeters ==
              markings.cornerArcRadiusMeters);
    }

    const PitchCircle circle = pitch.centerCircle();
    REQUIRE(circle.center == pitch.center());
    REQUIRE(circle.radiusMeters <= pitch.lengthMeters() / 2.0);
    REQUIRE(circle.radiusMeters <= pitch.widthMeters() / 2.0);
    REQUIRE(pitch.contains({.x = circle.center.x - circle.radiusMeters, .y = circle.center.y}));
    REQUIRE(pitch.contains({.x = circle.center.x, .y = circle.center.y + circle.radiusMeters}));
    REQUIRE(circle.contains(circle.center));

    const PitchCircle arc = pitch.cornerArc(PitchCorner::kMinXMinY);
    REQUIRE(arc.contains(arc.center));
    REQUIRE_FALSE(arc.contains(pitch.cornerPosition(PitchCorner::kMaxXMaxY)));
  }
}

TEST_CASE("PitchCircle containment survives offsets whose squares overflow or underflow",
          "[pitch][markings]") {
  constexpr double kHuge = std::numeric_limits<double>::max();
  const PitchCircle hugeCircle{.center = {.x = 0.0, .y = 0.0}, .radiusMeters = kHuge / 4.0};
  REQUIRE(hugeCircle.contains({.x = kHuge / 4.0, .y = 0.0}));
  REQUIRE_FALSE(hugeCircle.contains({.x = kHuge / 4.0, .y = kHuge / 4.0}));
  REQUIRE_FALSE(hugeCircle.contains({.x = kHuge, .y = kHuge}));

  constexpr double kTiny = 1e-200;
  const PitchCircle tinyCircle{.center = {.x = 0.0, .y = 0.0}, .radiusMeters = kTiny};
  REQUIRE(tinyCircle.contains({.x = kTiny, .y = 0.0}));
  REQUIRE_FALSE(tinyCircle.contains({.x = kTiny, .y = kTiny}));
}

TEST_CASE("Pitch goals stand centered on both goal lines", "[pitch][goal]") {
  const Pitch pitch(kStandardLengthMeters, kStandardWidthMeters);
  const Goal nearGoal = pitch.goal(GoalEnd::kMinX);
  const Goal farGoal = pitch.goal(GoalEnd::kMaxX);

  REQUIRE(pitch.goalLineX(GoalEnd::kMinX) == 0.0);
  REQUIRE(pitch.goalLineX(GoalEnd::kMaxX) == kStandardLengthMeters);
  REQUIRE(nearGoal.center == Vec2{.x = 0.0, .y = 34.0});
  REQUIRE(farGoal.center == Vec2{.x = 105.0, .y = 34.0});
  REQUIRE(nearGoal.widthMeters == 7.32);
  REQUIRE(nearGoal.heightMeters == 2.44);
  REQUIRE(nearGoal.widthMeters == farGoal.widthMeters);
  REQUIRE(nearGoal.heightMeters == farGoal.heightMeters);
  REQUIRE_THAT(nearGoal.postAtMinY().y, WithinAbs(30.34, kTolerance));
  REQUIRE_THAT(nearGoal.postAtMaxY().y, WithinAbs(37.66, kTolerance));
  REQUIRE(nearGoal.postAtMinY().x == 0.0);
  REQUIRE(farGoal.postAtMaxY().x == 105.0);
}

TEST_CASE("A goal frames what is between its posts and under its crossbar", "[pitch][goal]") {
  const Pitch pitch(kStandardLengthMeters, kStandardWidthMeters);
  const Goal goal = pitch.goal(GoalEnd::kMinX);
  const double minPostY = goal.postAtMinY().y;
  const double maxPostY = goal.postAtMaxY().y;

  REQUIRE(goal.framesPoint(goal.center.y, 0.0));
  REQUIRE(goal.framesPoint(goal.center.y, goal.heightMeters));
  REQUIRE(goal.framesPoint(minPostY, goal.heightMeters));
  REQUIRE(goal.framesPoint(maxPostY, 0.0));

  // Beside a post, or above the crossbar, is a miss.
  REQUIRE_FALSE(goal.framesPoint(std::nextafter(minPostY, 0.0), 0.0));
  REQUIRE_FALSE(goal.framesPoint(std::nextafter(maxPostY, 68.0), 0.0));
  REQUIRE_FALSE(goal.framesPoint(goal.center.y, std::nextafter(goal.heightMeters, 3.0)));
  REQUIRE_FALSE(goal.framesPoint(goal.center.y, -0.001));
  REQUIRE_FALSE(goal.framesPoint(goal.center.y, std::numeric_limits<double>::quiet_NaN()));
  REQUIRE_FALSE(goal.framesPoint(std::numeric_limits<double>::quiet_NaN(), 1.0));

  REQUIRE(goal.isBetweenPosts(minPostY));
  REQUIRE_FALSE(goal.isBetweenPosts(0.0));
  REQUIRE(goal.isUnderCrossbar(goal.heightMeters));
  REQUIRE_FALSE(goal.isUnderCrossbar(goal.heightMeters + 0.01));
}

TEST_CASE("Penalty and goal areas reach from their goal line into the pitch", "[pitch][markings]") {
  const Pitch pitch(kStandardLengthMeters, kStandardWidthMeters);

  const PitchRect nearPenalty = pitch.penaltyArea(GoalEnd::kMinX);
  REQUIRE(nearPenalty.min.x == 0.0);
  REQUIRE_THAT(nearPenalty.max.x, WithinAbs(16.5, kTolerance));
  REQUIRE_THAT(nearPenalty.min.y, WithinAbs(13.84, kTolerance));
  REQUIRE_THAT(nearPenalty.max.y, WithinAbs(54.16, kTolerance));

  const PitchRect farPenalty = pitch.penaltyArea(GoalEnd::kMaxX);
  REQUIRE_THAT(farPenalty.min.x, WithinAbs(88.5, kTolerance));
  REQUIRE(farPenalty.max.x == 105.0);
  REQUIRE(farPenalty.min.y == nearPenalty.min.y);
  REQUIRE(farPenalty.max.y == nearPenalty.max.y);

  const PitchRect nearGoalArea = pitch.goalArea(GoalEnd::kMinX);
  REQUIRE(nearGoalArea.min.x == 0.0);
  REQUIRE_THAT(nearGoalArea.max.x, WithinAbs(5.5, kTolerance));
  REQUIRE_THAT(nearGoalArea.min.y, WithinAbs(24.84, kTolerance));
  REQUIRE_THAT(nearGoalArea.max.y, WithinAbs(43.16, kTolerance));
}

TEST_CASE("Pitch tells a position inside a penalty area from one outside", "[pitch][markings]") {
  const Pitch pitch(kStandardLengthMeters, kStandardWidthMeters);
  const PitchRect area = pitch.penaltyArea(GoalEnd::kMinX);

  REQUIRE(pitch.isInPenaltyArea(GoalEnd::kMinX, {.x = 11.0, .y = 34.0}));
  REQUIRE(pitch.isInPenaltyArea(GoalEnd::kMinX, area.min));
  REQUIRE(pitch.isInPenaltyArea(GoalEnd::kMinX, area.max));
  REQUIRE(pitch.isInPenaltyArea(GoalEnd::kMinX, {.x = area.max.x, .y = 34.0}));

  // Just past the line, in the other area, or off the pitch entirely.
  REQUIRE_FALSE(
      pitch.isInPenaltyArea(GoalEnd::kMinX, {.x = std::nextafter(area.max.x, 20.0), .y = 34.0}));
  REQUIRE_FALSE(
      pitch.isInPenaltyArea(GoalEnd::kMinX, {.x = 11.0, .y = std::nextafter(area.max.y, 60.0)}));
  REQUIRE_FALSE(pitch.isInPenaltyArea(GoalEnd::kMinX, {.x = 94.0, .y = 34.0}));
  REQUIRE(pitch.isInPenaltyArea(GoalEnd::kMaxX, {.x = 94.0, .y = 34.0}));
  REQUIRE_FALSE(pitch.isInPenaltyArea(GoalEnd::kMinX,
                                      {.x = 11.0, .y = std::numeric_limits<double>::quiet_NaN()}));
}

TEST_CASE("Penalty spots, the center circle and the corner arcs sit where the Laws put them",
          "[pitch][markings]") {
  const Pitch pitch(kStandardLengthMeters, kStandardWidthMeters);

  REQUIRE(pitch.penaltySpot(GoalEnd::kMinX) == Vec2{.x = 11.0, .y = 34.0});
  REQUIRE(pitch.penaltySpot(GoalEnd::kMaxX) == Vec2{.x = 94.0, .y = 34.0});
  REQUIRE(pitch.center() == Vec2{.x = 52.5, .y = 34.0});

  const PitchCircle circle = pitch.centerCircle();
  REQUIRE(circle.center == pitch.center());
  REQUIRE(circle.radiusMeters == 9.15);
  REQUIRE(circle.contains(circle.center));
  REQUIRE(circle.contains({.x = 52.5, .y = 34.0 + 9.15}));
  REQUIRE_FALSE(circle.contains({.x = 52.5, .y = 34.0 + 9.16}));
  REQUIRE_FALSE(circle.contains({.x = 52.5, .y = std::numeric_limits<double>::quiet_NaN()}));

  const std::array corners{PitchCorner::kMinXMinY, PitchCorner::kMinXMaxY, PitchCorner::kMaxXMinY,
                           PitchCorner::kMaxXMaxY};
  const std::array expected{Vec2{.x = 0.0, .y = 0.0}, Vec2{.x = 0.0, .y = 68.0},
                            Vec2{.x = 105.0, .y = 0.0}, Vec2{.x = 105.0, .y = 68.0}};
  for (std::size_t index = 0; index < corners.size(); ++index) {
    CAPTURE(index);
    const PitchCircle arc = pitch.cornerArc(corners.at(index));
    REQUIRE(pitch.cornerPosition(corners.at(index)) == expected.at(index));
    REQUIRE(arc.center == expected.at(index));
    REQUIRE(arc.radiusMeters == 1.0);
    REQUIRE(arc.contains(arc.center));
  }

  const PitchCircle arc = pitch.cornerArc(PitchCorner::kMinXMinY);
  REQUIRE(arc.contains({.x = 1.0, .y = 0.0}));
  REQUIRE(arc.contains({.x = 0.0, .y = 1.0}));
  REQUIRE_FALSE(arc.contains({.x = 1.0, .y = 1.0}));
}

TEST_CASE("Pitch measures the distance to the nearest point of a goal", "[pitch][goal]") {
  const Pitch pitch(kStandardLengthMeters, kStandardWidthMeters);
  const Goal goal = pitch.goal(GoalEnd::kMinX);

  REQUIRE_THAT(pitch.distanceToGoalMeters(GoalEnd::kMinX, pitch.center()),
               WithinAbs(52.5, kTolerance));
  REQUIRE_THAT(pitch.distanceToGoalMeters(GoalEnd::kMaxX, pitch.center()),
               WithinAbs(52.5, kTolerance));
  REQUIRE(pitch.distanceToGoalMeters(GoalEnd::kMinX, goal.center) == 0.0);
  REQUIRE(pitch.distanceToGoalMeters(GoalEnd::kMinX, goal.postAtMaxY()) == 0.0);

  // Beside the goal the distance runs to the nearer post, not to the center.
  REQUIRE_THAT(pitch.distanceToGoalMeters(GoalEnd::kMinX, {.x = 0.0, .y = 41.0}),
               WithinAbs(3.34, kTolerance));
  REQUIRE_THAT(pitch.distanceToGoalMeters(GoalEnd::kMinX, {.x = 3.0, .y = 34.0}),
               WithinAbs(3.0, kTolerance));
  REQUIRE_THAT(pitch.distanceToGoalMeters(GoalEnd::kMaxX, {.x = 100.0, .y = 34.0}),
               WithinAbs(5.0, kTolerance));
  REQUIRE(std::isnan(pitch.distanceToGoalMeters(
      GoalEnd::kMinX, {.x = std::numeric_limits<double>::quiet_NaN(), .y = 34.0})));
}

TEST_CASE("Pitch measures the open angle a goal subtends", "[pitch][goal]") {
  const Pitch pitch(kStandardLengthMeters, kStandardWidthMeters);
  const Goal goal = pitch.goal(GoalEnd::kMinX);

  // From the penalty spot: twice the angle to one post, 2·atan(3.66 / 11).
  REQUIRE_THAT(pitch.goalAngleRadians(GoalEnd::kMinX, pitch.penaltySpot(GoalEnd::kMinX)),
               WithinAbs(2.0 * std::atan(3.66 / 11.0), 1e-9));
  // Between the posts the goal fills the half plane; on a post there is no angle.
  REQUIRE_THAT(pitch.goalAngleRadians(GoalEnd::kMinX, goal.center),
               WithinAbs(std::numbers::pi, kTolerance));
  REQUIRE(pitch.goalAngleRadians(GoalEnd::kMinX, goal.postAtMinY()) == 0.0);
  REQUIRE(pitch.goalAngleRadians(
              GoalEnd::kMinX, {.x = 20.0, .y = std::numeric_limits<double>::infinity()}) == 0.0);

  // It narrows with distance, mirrors across the goal's center line, and is the
  // same at either end of the pitch.
  double previous = std::numbers::pi;
  for (int meters = 1; meters <= 100; ++meters) {
    const auto depth = static_cast<double>(meters);
    CAPTURE(depth);
    const double angle = pitch.goalAngleRadians(GoalEnd::kMinX, {.x = depth, .y = 34.0});
    REQUIRE(angle < previous);
    REQUIRE_THAT(pitch.goalAngleRadians(GoalEnd::kMaxX, {.x = 105.0 - depth, .y = 34.0}),
                 WithinAbs(angle, kTolerance));
    previous = angle;
  }
  REQUIRE_THAT(
      pitch.goalAngleRadians(GoalEnd::kMinX, {.x = 20.0, .y = 41.0}),
      WithinAbs(pitch.goalAngleRadians(GoalEnd::kMinX, {.x = 20.0, .y = 27.0}), kTolerance));

  // At the same distance from the goal, in front of the center beats the flank.
  const Vec2 central{.x = 20.0, .y = 34.0};
  const Vec2 flank{.x = 12.0, .y = 53.66};
  REQUIRE_THAT(pitch.distanceToGoalMeters(GoalEnd::kMinX, flank),
               WithinAbs(pitch.distanceToGoalMeters(GoalEnd::kMinX, central), 1e-9));
  REQUIRE(pitch.goalAngleRadians(GoalEnd::kMinX, central) >
          pitch.goalAngleRadians(GoalEnd::kMinX, flank));
}
