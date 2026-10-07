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
  // Without the ball: protect the space behind a teammate who may step out,
  // or behind the teammate pressing the carrier.
  kCover,
  // Without the ball: close the carrier down on the line to a passing option.
  kPressCarrier,
  // Without the ball: stand in the lane from the carrier to a receiver.
  kBlockLane,
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

// A goalkeeper's judgement of a free ball the opponent played
// (docs/goalkeeper.md): which ball -- the player and tick of its last touch
// -- how far he misjudges his head start on it, and whether he decided to
// come for it. He draws his misjudgement of each ball once and keeps it while
// he follows that ball.
struct SweepJudgement {
  SimCore::PlayerId touchedBy;
  SimCore::SimTick touchedAt;
  double misjudgement = 0.0;
  bool coming = false;

  friend bool operator==(const SweepJudgement&, const SweepJudgement&) = default;
};

// A point in a goalkeeper's plane, the upright plane through where he stood
// when he went, square to the ball's way: how far across it, positive to the
// left of the ball's way, and how high above the ground
// (docs/shot-stopping.md).
struct PlanePoint {
  double across = 0.0;  // m
  double up = 0.0;      // m

  friend bool operator==(const PlanePoint&, const PlanePoint&) = default;
};

// A goalkeeper's answer to one flight of a shot, decided once he has reacted
// to it: against the touch that sent the ball on its way -- the strike or a
// deflection, by player and tick --, in which tick he went, and the plane he
// stood in, as the point it passes through and the ball's way along the
// ground as a unit vector. He runs across it to `feet`, which takes him
// runSeconds, then sends his hands to `target`, measured from his feet there.
// landSeconds after he went the ball reaches him, and he is down for
// recoverySeconds more. All seconds count from the start of `tick`. A keeper
// who leaves the ball neither runs nor dives, and is never down.
struct KeeperDive {
  SimCore::PlayerId touchedBy;
  SimCore::SimTick touchedAt;
  SimCore::SimTick tick;
  SimCore::Vec2 origin;
  SimCore::Vec2 normal;
  double feet = 0.0;  // m
  PlanePoint target;
  double runSeconds = 0.0;
  double landSeconds = 0.0;
  double recoverySeconds = 0.0;

  friend bool operator==(const KeeperDive&, const KeeperDive&) = default;
};

// A player's tactical runtime state, kept in the match state because
// systems keep nothing between ticks. Empty for a player of a scripted side,
// but for his last jump.
struct PlayerTacticalState {
  std::optional<DesiredRegion> region;
  std::optional<PlayerAction> action;
  // When he last challenged the carrier for the ball (docs/pressing.md).
  std::optional<SimCore::SimTick> lastChallenge;
  // A goalkeeper's judgement of the last free ball he could come for.
  std::optional<SweepJudgement> sweep;
  // A goalkeeper's answer to the last shot he faced, until he is up again.
  std::optional<KeeperDive> dive;
  // When he last went up for a high ball (docs/aerial-duels.md): he goes up
  // for no other until he has landed.
  std::optional<SimCore::SimTick> lastJump;

  friend bool operator==(const PlayerTacticalState&, const PlayerTacticalState&) = default;
};

}  // namespace ElyverseFootball::SimMatch
