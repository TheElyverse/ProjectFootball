#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

#include "ids.hpp"
#include "simTime.hpp"
#include "vec2.hpp"

namespace ElyverseFootball::SimMatch {

// The parts of a candidate position's cost (implementation plan section 7.2,
// docs/desired-region.md), each inspectable on its own before the tactic's
// positioning weights combine them into total.
struct PositionCost {
  // Distance from the tactical target, in units of targetDistanceScale.
  double targetDistance = 0.0;
  // Crowding: teammates closer than the spacing radius.
  double spacing = 0.0;
  // Opponents closer than the pressure radius.
  double pressure = 0.0;
  // How much of the position the opponent controls, in [0, 1].
  double occupancy = 0.0;
  // How far ahead of the ball the position leaves the team if it loses it,
  // in [0, 1]; 0 without the ball.
  double transitionRisk = 0.0;
  // The weighted sum.
  double total = 0.0;

  friend bool operator==(const PositionCost&, const PositionCost&) = default;
};

// Where a player wants to be: the tactical target his shape gives him, the
// centre of the region he settled on around it, and what that centre costs.
// The centre is his movement target.
struct DesiredRegion {
  SimCore::Vec2 tacticalTarget;
  SimCore::Vec2 center;
  PositionCost cost;

  friend bool operator==(const DesiredRegion&, const DesiredRegion&) = default;
};

// What a player without the ball has decided to do (docs/off-ball-movement.md).
enum class ActionType : std::uint8_t {
  // Stay in the desired region.
  kHoldPosition,
  // Offer the ball carrier a short pass with an open lane.
  kSupportCarrier,
  // Move to nearby space his team controls.
  kMoveIntoSpace,
  // Run into the space behind the opponent's defensive line.
  kRunInBehind,
  // Stretch the play on the wing of his side of the pitch.
  kCreateWidth,
  // Take up the halfspace on his side of the pitch.
  kOccupyHalfspace,
  // Without the ball: stay goal-side of an opponent in his zone.
  kMarkOpponent,
  // Without the ball: follow an opponent running at the goal.
  kTrackRunner,
  // Without the ball: protect the space behind a teammate who may step out.
  kCover,
};

// "holdPosition", "supportCarrier", ...; "unknown" outside the enumerators.
[[nodiscard]] std::string_view actionName(ActionType type) noexcept;

// A decided action: where it takes the player, whom it is about if anyone,
// when he decided it and whether his team had the ball then. He keeps it
// until he decides again.
struct PlayerAction {
  ActionType type = ActionType::kHoldPosition;
  SimCore::Vec2 target;
  std::optional<SimCore::PlayerId> subject;
  SimCore::SimTick decidedAt;
  bool withBall = true;

  friend bool operator==(const PlayerAction&, const PlayerAction&) = default;
};

// A player's tactical runtime state, kept in the match state because
// systems keep nothing between ticks. Empty for a player of a scripted side.
struct PlayerTacticalState {
  std::optional<DesiredRegion> region;
  std::optional<PlayerAction> action;

  friend bool operator==(const PlayerTacticalState&, const PlayerTacticalState&) = default;
};

}  // namespace ElyverseFootball::SimMatch
