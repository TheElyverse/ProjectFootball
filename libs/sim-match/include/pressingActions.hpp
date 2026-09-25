#pragma once

#include <optional>

#include "vec2.hpp"

namespace ElyverseFootball::SimMatch {

// The geometry of the individual actions pressing is built from (PF-012,
// docs/pressing.md): pressing the carrier, blocking a passing lane and
// covering the presser. Pure functions of positions, so each can be tested
// on its own; the defensive decisions (defensiveActions.hpp) offer them as
// candidates.

// Where a presser runs: pressDistance from the carrier on the carrier's line
// to the option he wants to cut off, so his approach closes that pass while
// it closes the carrier down -- an angle, not the shortest path. Without an
// option, goal-side of the carrier, toward the defending side's goal.
[[nodiscard]] SimCore::Vec2 pressTarget(SimCore::Vec2 carrier,
                                        const std::optional<SimCore::Vec2>& option,
                                        SimCore::Vec2 ownGoal, double pressDistance) noexcept;

// The straight line a pass would take, from the carrier to a receiver.
struct PassingLane {
  SimCore::Vec2 carrier;
  SimCore::Vec2 receiver;
};

// Where a player blocks a passing lane: the point of the lane nearest to him,
// but at least minDistance from either end, so he stands between them rather
// than on top of one. The middle of the lane if it is shorter than twice
// that.
[[nodiscard]] SimCore::Vec2 laneBlockTarget(const PassingLane& lane, SimCore::Vec2 blocker,
                                            double minDistance) noexcept;

// Where a player covers a presser: distance behind him toward the own goal,
// to catch the carrier if he gets past.
[[nodiscard]] SimCore::Vec2 coverTarget(SimCore::Vec2 presser, SimCore::Vec2 ownGoal,
                                        double distance) noexcept;

}  // namespace ElyverseFootball::SimMatch
