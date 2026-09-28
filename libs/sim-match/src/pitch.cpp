#include "pitch.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>

#include "stableMath.hpp"

namespace ElyverseFootball::SimMatch {
namespace {

// How a pitch's markings scale against the standard pitch: depths along the
// length, widths across the width, and radii with the smaller of the two, so a
// circle stays a circle and still fits a pitch that is short or narrow.
struct MarkingScale {
  double depth;
  double width;
  double radius;
};

[[nodiscard]] MarkingScale scaleOf(const Pitch& pitch) noexcept {
  const double depth = pitch.lengthMeters() / kStandardLengthMeters;
  const double width = pitch.widthMeters() / kStandardWidthMeters;
  return {.depth = depth, .width = width, .radius = std::min(depth, width)};
}

// How far an area reaches from its goal line, and how wide it is.
struct AreaSize {
  double depthMeters;
  double widthMeters;
};

// A rectangle reaching from a goal line into the pitch, centered on the goal.
[[nodiscard]] PitchRect areaRect(const Pitch& pitch, const GoalEnd end,
                                 const AreaSize size) noexcept {
  const double goalLine = pitch.goalLineX(end);
  const double inner =
      end == GoalEnd::kMinX ? goalLine + size.depthMeters : goalLine - size.depthMeters;
  const double halfWidth = size.widthMeters / 2.0;
  const double centerY = pitch.widthMeters() / 2.0;
  return {.min = {.x = std::min(goalLine, inner), .y = centerY - halfWidth},
          .max = {.x = std::max(goalLine, inner), .y = centerY + halfWidth}};
}

// A direction scaled to a largest component of one. Scaling by a positive
// factor leaves a direction, and the angle between two of them, unchanged,
// while the products below can then neither overflow nor underflow, whatever
// the coordinates.
[[nodiscard]] SimCore::Vec2 scaledToUnitComponent(const SimCore::Vec2 direction) noexcept {
  const double largest = std::max(std::abs(direction.x), std::abs(direction.y));
  if (largest == 0.0) {
    return direction;
  }
  return {.x = direction.x / largest, .y = direction.y / largest};
}

}  // namespace

bool Goal::isBetweenPosts(const double pitchY) const noexcept {
  return pitchY >= postAtMinY().y && pitchY <= postAtMaxY().y;
}

bool Goal::isUnderCrossbar(const double height) const noexcept {
  return height >= 0.0 && height <= heightMeters;
}

bool Goal::framesPoint(const double pitchY, const double height) const noexcept {
  return isBetweenPosts(pitchY) && isUnderCrossbar(height);
}

Pitch::Pitch(const double lengthMeters, const double widthMeters)
    : lengthMeters_(lengthMeters), widthMeters_(widthMeters) {
  if (!std::isfinite(lengthMeters) || lengthMeters <= 0.0 || !std::isfinite(widthMeters) ||
      widthMeters <= 0.0) {
    throw std::invalid_argument("Pitch dimensions must be positive, finite meters");
  }
}

bool Pitch::contains(const SimCore::Vec2 position) const noexcept {
  return position.isFinite() && position.x >= 0.0 && position.x <= lengthMeters_ &&
         position.y >= 0.0 && position.y <= widthMeters_;
}

SimCore::Vec2 Pitch::clamp(const SimCore::Vec2 position) const noexcept {
  return {.x = std::clamp(position.x, 0.0, lengthMeters_),
          .y = std::clamp(position.y, 0.0, widthMeters_)};
}

SimCore::Vec2 Pitch::center() const noexcept {
  return {.x = lengthMeters_ / 2.0, .y = widthMeters_ / 2.0};
}

PitchMarkings Pitch::markings() const noexcept {
  const MarkingScale scale = scaleOf(*this);
  return {
      .goalWidthMeters = kStandardMarkings.goalWidthMeters * scale.width,
      .goalHeightMeters = kStandardMarkings.goalHeightMeters * scale.width,
      .penaltyAreaDepthMeters = kStandardMarkings.penaltyAreaDepthMeters * scale.depth,
      .penaltyAreaWidthMeters = kStandardMarkings.penaltyAreaWidthMeters * scale.width,
      .goalAreaDepthMeters = kStandardMarkings.goalAreaDepthMeters * scale.depth,
      .goalAreaWidthMeters = kStandardMarkings.goalAreaWidthMeters * scale.width,
      .penaltySpotDistanceMeters = kStandardMarkings.penaltySpotDistanceMeters * scale.depth,
      .centerCircleRadiusMeters = kStandardMarkings.centerCircleRadiusMeters * scale.radius,
      .cornerArcRadiusMeters = kStandardMarkings.cornerArcRadiusMeters * scale.radius,
  };
}

double Pitch::goalLineX(const GoalEnd end) const noexcept {
  return end == GoalEnd::kMinX ? 0.0 : lengthMeters_;
}

SimCore::Vec2 Pitch::cornerPosition(const PitchCorner corner) const noexcept {
  const bool atMaxX = corner == PitchCorner::kMaxXMinY || corner == PitchCorner::kMaxXMaxY;
  const bool atMaxY = corner == PitchCorner::kMinXMaxY || corner == PitchCorner::kMaxXMaxY;
  return {.x = atMaxX ? lengthMeters_ : 0.0, .y = atMaxY ? widthMeters_ : 0.0};
}

Goal Pitch::goal(const GoalEnd end) const noexcept {
  const PitchMarkings marks = markings();
  return {.center = {.x = goalLineX(end), .y = widthMeters_ / 2.0},
          .widthMeters = marks.goalWidthMeters,
          .heightMeters = marks.goalHeightMeters};
}

PitchRect Pitch::penaltyArea(const GoalEnd end) const noexcept {
  const PitchMarkings marks = markings();
  return areaRect(
      *this, end,
      {.depthMeters = marks.penaltyAreaDepthMeters, .widthMeters = marks.penaltyAreaWidthMeters});
}

PitchRect Pitch::goalArea(const GoalEnd end) const noexcept {
  const PitchMarkings marks = markings();
  return areaRect(
      *this, end,
      {.depthMeters = marks.goalAreaDepthMeters, .widthMeters = marks.goalAreaWidthMeters});
}

SimCore::Vec2 Pitch::penaltySpot(const GoalEnd end) const noexcept {
  const double distance = markings().penaltySpotDistanceMeters;
  const double goalLine = goalLineX(end);
  return {.x = end == GoalEnd::kMinX ? goalLine + distance : goalLine - distance,
          .y = widthMeters_ / 2.0};
}

PitchCircle Pitch::centerCircle() const noexcept {
  return {.center = center(), .radiusMeters = markings().centerCircleRadiusMeters};
}

PitchCircle Pitch::cornerArc(const PitchCorner corner) const noexcept {
  return {.center = cornerPosition(corner), .radiusMeters = markings().cornerArcRadiusMeters};
}

bool Pitch::isInPenaltyArea(const GoalEnd end, const SimCore::Vec2 position) const noexcept {
  return position.isFinite() && penaltyArea(end).contains(position);
}

double Pitch::distanceToGoalMeters(const GoalEnd end, const SimCore::Vec2 position) const noexcept {
  const Goal frame = goal(end);
  const double nearestY = std::clamp(position.y, frame.postAtMinY().y, frame.postAtMaxY().y);
  return SimCore::distance(position, {.x = frame.center.x, .y = nearestY});
}

double Pitch::goalAngleRadians(const GoalEnd end, const SimCore::Vec2 position) const noexcept {
  const Goal frame = goal(end);
  const SimCore::Vec2 toMinPost = scaledToUnitComponent(frame.postAtMinY() - position);
  const SimCore::Vec2 toMaxPost = scaledToUnitComponent(frame.postAtMaxY() - position);
  if (!toMinPost.isFinite() || !toMaxPost.isFinite() || toMinPost.lengthSquared() == 0.0 ||
      toMaxPost.lengthSquared() == 0.0) {
    return 0.0;
  }

  // The angle between the two post directions the way std::atan2 would take
  // it, from the perpendicular part over the parallel part, but with the stable
  // arctangent. Scaling leaves both parts in the same proportion, so the
  // quotient, and with it the angle, is the one the unscaled directions have.
  const double perpendicular = std::abs((toMinPost.x * toMaxPost.y) - (toMinPost.y * toMaxPost.x));
  const double parallel = toMinPost.dot(toMaxPost);
  constexpr double kRightAngle = std::numbers::pi / 2.0;
  if (parallel == 0.0) {
    return kRightAngle;
  }
  const double acute = SimCore::stableArcTangent(std::abs(perpendicular / parallel));
  return parallel > 0.0 ? acute : std::numbers::pi - acute;
}

}  // namespace ElyverseFootball::SimMatch
