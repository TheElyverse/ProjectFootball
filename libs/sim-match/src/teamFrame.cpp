#include "teamFrame.hpp"

namespace ElyverseFootball::SimMatch {

double attackingDirection(const TeamSide side) noexcept {
  return side == TeamSide::kHome ? 1.0 : -1.0;
}

double depthOf(const TeamSide side, const SimCore::Vec2 position, const Pitch& pitch) noexcept {
  return side == TeamSide::kHome ? position.x : pitch.lengthMeters() - position.x;
}

double xAtDepth(const TeamSide side, const double depth, const Pitch& pitch) noexcept {
  return side == TeamSide::kHome ? depth : pitch.lengthMeters() - depth;
}

Goal attackedGoal(const Pitch& pitch, const TeamSide side) noexcept {
  return pitch.goal(attackingDirection(side) > 0.0 ? GoalEnd::kMaxX : GoalEnd::kMinX);
}

}  // namespace ElyverseFootball::SimMatch
