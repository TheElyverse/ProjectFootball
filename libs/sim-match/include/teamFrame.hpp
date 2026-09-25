#pragma once

#include "matchState.hpp"
#include "pitch.hpp"
#include "vec2.hpp"

namespace ElyverseFootball::SimMatch {

// Team-relative coordinates. Tactics speak of depth -- how far up the pitch
// something is from a team's own goal line -- while the state uses fixed
// pitch coordinates. These convert between the two, using
// attackingDirection() (passCandidates.hpp): home defends x = 0.

// The other side.
[[nodiscard]] constexpr TeamSide opponentOf(const TeamSide side) noexcept {
  return side == TeamSide::kHome ? TeamSide::kAway : TeamSide::kHome;
}

// How far a position is from the side's own goal line, along the pitch
// length: x for home, length - x for away. 0 on the own goal line, the pitch
// length on the opponent's.
[[nodiscard]] double depthOf(TeamSide side, SimCore::Vec2 position, const Pitch& pitch) noexcept;

// The pitch x of a depth: the inverse of depthOf() along the length.
[[nodiscard]] double xAtDepth(TeamSide side, double depth, const Pitch& pitch) noexcept;

}  // namespace ElyverseFootball::SimMatch
