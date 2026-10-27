#pragma once

#include "matchState.hpp"
#include "pitch.hpp"
#include "vec2.hpp"

namespace ElyverseFootball::SimMatch {

// Team-relative coordinates. Tactics speak of depth -- how far up the pitch
// something is from a team's own goal line -- while the state uses fixed
// pitch coordinates. These convert between the two, using
// attackingDirection(): home defends x = 0.

// The other side.
[[nodiscard]] constexpr TeamSide opponentOf(const TeamSide side) noexcept {
  return side == TeamSide::kHome ? TeamSide::kAway : TeamSide::kHome;
}

// The direction along x a side attacks: +1 for home, -1 for away. Fixed in
// the sandbox, which has no halves and in which every scenario places home at
// x = 0; the one place to change once rules let teams switch ends.
[[nodiscard]] double attackingDirection(TeamSide side) noexcept;

// The end of the pitch whose goal the side defends.
[[nodiscard]] constexpr GoalEnd ownGoalEnd(const TeamSide side) noexcept {
  return side == TeamSide::kHome ? GoalEnd::kMinX : GoalEnd::kMaxX;
}

// How far a position is from the side's own goal line, along the pitch
// length: x for home, length - x for away. 0 on the own goal line, the pitch
// length on the opponent's.
[[nodiscard]] double depthOf(TeamSide side, SimCore::Vec2 position, const Pitch& pitch) noexcept;

// The pitch x of a depth: the inverse of depthOf() along the length.
[[nodiscard]] double xAtDepth(TeamSide side, double depth, const Pitch& pitch) noexcept;

// The goal a side attacks: the one at the end attackingDirection() points to.
[[nodiscard]] Goal attackedGoal(const Pitch& pitch, TeamSide side) noexcept;

}  // namespace ElyverseFootball::SimMatch
