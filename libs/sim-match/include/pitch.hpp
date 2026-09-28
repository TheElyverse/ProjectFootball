#pragma once

#include <cstdint>
#include <stdexcept>

#include "vec2.hpp"

namespace ElyverseFootball::SimMatch {

// An axis-aligned rectangle in pitch coordinates, in meters, edges included:
// a region such as a zone or part of a grid. It may reach past the pitch.
struct PitchRect {
  SimCore::Vec2 min;
  SimCore::Vec2 max;

  [[nodiscard]] bool contains(const SimCore::Vec2 point) const noexcept {
    return point.x >= min.x && point.x <= max.x && point.y >= min.y && point.y <= max.y;
  }

  friend bool operator==(const PitchRect&, const PitchRect&) = default;
};

// A circle in pitch coordinates, in meters, edge included: the center circle,
// or the circle a corner arc is the quarter of that lies on the pitch.
struct PitchCircle {
  SimCore::Vec2 center;
  double radiusMeters = 0.0;

  // Includes the edge, without an implicit epsilon. A non-finite point is never
  // inside. Compares lengths rather than squares: on the extreme pitch sizes
  // the constructor accepts, squaring the offset and the radius would overflow
  // (or underflow) and let points outside the circle count as inside.
  [[nodiscard]] bool contains(const SimCore::Vec2 point) const noexcept {
    const SimCore::Vec2 offset = point - center;
    return offset.isFinite() && offset.length() <= radiusMeters;
  }

  friend bool operator==(const PitchCircle&, const PitchCircle&) = default;
};

// Standard eleven-a-side dimensions: the reference pitch the Laws of the Game
// state every marking for, and what the markings of another size scale from.
inline constexpr double kStandardLengthMeters = 105.0;
inline constexpr double kStandardWidthMeters = 68.0;

// The sandbox size the seven-a-side fixtures and scenarios play on. Not a
// mandated 7v7 size -- a size small enough to read in the debug viewer.
inline constexpr double kSandboxLengthMeters = 60.0;
inline constexpr double kSandboxWidthMeters = 40.0;

// The markings of a pitch, in meters: the lengths the goals, the areas, the
// spots and the arcs are built from (Laws of the Game 1). Depths are measured
// from a goal line, widths across the pitch.
//
// The Laws fix every one of these and give a range only for the pitch itself,
// so the two dimensions are the only thing to configure; a pitch of another
// size scales the standard markings below (docs/match-geometry.md).
struct PitchMarkings {
  // Between the posts, and from the ground to the underside of the crossbar.
  double goalWidthMeters = 0.0;
  double goalHeightMeters = 0.0;
  double penaltyAreaDepthMeters = 0.0;
  double penaltyAreaWidthMeters = 0.0;
  double goalAreaDepthMeters = 0.0;
  double goalAreaWidthMeters = 0.0;
  // From the goal line to the penalty spot.
  double penaltySpotDistanceMeters = 0.0;
  double centerCircleRadiusMeters = 0.0;
  double cornerArcRadiusMeters = 0.0;

  friend bool operator==(const PitchMarkings&, const PitchMarkings&) = default;
};

// The Laws' own numbers, at the standard pitch. A goal is 7.32 by 2.44 meters;
// the penalty area reaches 16.5 meters in front of the goal line and 16.5 to
// either side of the goal (16.5 + 7.32 + 16.5 = 40.32), the goal area 5.5 the
// same way (5.5 + 7.32 + 5.5 = 18.32).
inline constexpr PitchMarkings kStandardMarkings{
    .goalWidthMeters = 7.32,
    .goalHeightMeters = 2.44,
    .penaltyAreaDepthMeters = 16.5,
    .penaltyAreaWidthMeters = 40.32,
    .goalAreaDepthMeters = 5.5,
    .goalAreaWidthMeters = 18.32,
    .penaltySpotDistanceMeters = 11.0,
    .centerCircleRadiusMeters = 9.15,
    .cornerArcRadiusMeters = 1.0,
};

// Which of the two goals, by the goal line it stands on: x = 0 or x = length.
// Pitch coordinates stay fixed when teams change ends, so an end says nothing
// about which side defends it.
enum class GoalEnd : std::uint8_t {
  kMinX,
  kMaxX,
};

// One of the four corners, by the goal line and the touchline it lies on.
enum class PitchCorner : std::uint8_t {
  kMinXMinY,
  kMinXMaxY,
  kMaxXMinY,
  kMaxXMaxY,
};

// A goal frame: the two posts on its goal line, and the crossbar above them.
// The posts are points -- their own thickness is not modelled -- and the
// height is the underside of the crossbar.
//
// Heights are meters above the ground. The rest of the simulation is flat, so a
// height is something a caller supplies, never something this geometry stores.
struct Goal {
  // The middle between the posts, on the goal line.
  SimCore::Vec2 center;
  double widthMeters = 0.0;
  double heightMeters = 0.0;

  // The two posts, at the lower and the higher pitch y.
  [[nodiscard]] SimCore::Vec2 postAtMinY() const noexcept {
    return {.x = center.x, .y = center.y - (widthMeters / 2.0)};
  }
  [[nodiscard]] SimCore::Vec2 postAtMaxY() const noexcept {
    return {.x = center.x, .y = center.y + (widthMeters / 2.0)};
  }

  // Whether a pitch y lies between the posts, the posts included.
  [[nodiscard]] bool isBetweenPosts(double pitchY) const noexcept;

  // Whether a height above the ground stays under the crossbar: from the ground
  // to the crossbar's underside, both included. A negative height is below the
  // ground and never under it.
  [[nodiscard]] bool isUnderCrossbar(double height) const noexcept;

  // Whether a point at this pitch y and this height is inside the frame, so a
  // point beside a post or above the crossbar misses the goal. It says nothing
  // about whether the point is on the goal line or which way it travels:
  // whether a ball crossed the line is for the shot and the rules to decide.
  [[nodiscard]] bool framesPoint(double pitchY, double height) const noexcept;

  friend bool operator==(const Goal&, const Goal&) = default;
};

// Metric rectangle: (0, 0) is a corner, +x runs along the length toward the
// opposite goal line, +y along the width toward the opposite touchline.
// Coordinates are fixed to the pitch, independent of either team's direction.
class Pitch {
 public:
  // Both dimensions must be positive and finite, otherwise throws
  // std::invalid_argument. No default size or competition rules are imposed.
  explicit Pitch(double lengthMeters, double widthMeters);

  [[nodiscard]] double lengthMeters() const noexcept { return lengthMeters_; }
  [[nodiscard]] double widthMeters() const noexcept { return widthMeters_; }

  // Includes all edges and corners. Non-finite positions are never inside.
  // Tests a point only; ball radius and out-of-play rules belong elsewhere.
  [[nodiscard]] bool contains(SimCore::Vec2 position) const noexcept;

  // The point on the pitch nearest to a finite position: the position itself
  // if contains() holds, otherwise its projection onto the nearest edge or
  // corner.
  [[nodiscard]] SimCore::Vec2 clamp(SimCore::Vec2 position) const noexcept;

  // The center of the pitch: where the center circle and the kickoff are.
  [[nodiscard]] SimCore::Vec2 center() const noexcept;

  // The markings this pitch derives from its dimensions, in meters: the
  // standard markings scaled with the pitch. Depths scale with the length,
  // widths with the width, and radii with the smaller of the two factors, so
  // every marking fits inside the pitch whatever its shape.
  [[nodiscard]] PitchMarkings markings() const noexcept;

  // The x of a goal line: 0 or the length.
  [[nodiscard]] double goalLineX(GoalEnd end) const noexcept;

  // One corner of the pitch.
  [[nodiscard]] SimCore::Vec2 cornerPosition(PitchCorner corner) const noexcept;

  // The goal frame standing on this end's goal line, centered on it.
  [[nodiscard]] Goal goal(GoalEnd end) const noexcept;

  // The penalty area and the goal area in front of this end's goal: from the
  // goal line into the pitch, centered on the goal.
  [[nodiscard]] PitchRect penaltyArea(GoalEnd end) const noexcept;
  [[nodiscard]] PitchRect goalArea(GoalEnd end) const noexcept;

  // The penalty spot in front of this end's goal, on the goal's center line.
  [[nodiscard]] SimCore::Vec2 penaltySpot(GoalEnd end) const noexcept;

  // The center circle, and the full circle a corner arc is the quarter of that
  // lies on the pitch -- a position is inside the arc when it is both inside
  // this circle and on the pitch.
  [[nodiscard]] PitchCircle centerCircle() const noexcept;
  [[nodiscard]] PitchCircle cornerArc(PitchCorner corner) const noexcept;

  // Whether a position lies in this end's penalty area, its lines included.
  // A non-finite position never does.
  [[nodiscard]] bool isInPenaltyArea(GoalEnd end, SimCore::Vec2 position) const noexcept;

  // The distance in meters from a position to the nearest point of this end's
  // goal -- the line between the posts, not its center -- which is zero between
  // the posts. A non-finite position gives a non-finite distance.
  [[nodiscard]] double distanceToGoalMeters(GoalEnd end, SimCore::Vec2 position) const noexcept;

  // The angle in radians that this end's goal -- the line between the posts --
  // subtends at a position: how much goal a shot from there has, in [0, pi],
  // widest in front of the center and narrowing toward the posts and with
  // distance. It is the open angle of the empty pitch: nobody blocks it, and a
  // position behind the goal line or on the wrong side gets the same angle as
  // its mirror image, so callers check where they stand themselves. Zero on a
  // post, and zero for a non-finite position.
  //
  // Computed with SimCore::stableArcTangent(), so the same on every platform.
  [[nodiscard]] double goalAngleRadians(GoalEnd end, SimCore::Vec2 position) const noexcept;

  // Compares both dimensions exactly, like Vec2 does. Two pitches built from
  // the same numbers are the same pitch; nothing here applies a tolerance.
  // Markings follow from the dimensions, so they need no comparison of their
  // own.
  friend bool operator==(const Pitch&, const Pitch&) = default;

 private:
  double lengthMeters_;
  double widthMeters_;
};

}  // namespace ElyverseFootball::SimMatch
