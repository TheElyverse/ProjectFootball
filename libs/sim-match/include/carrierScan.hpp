#pragma once

#include <cstddef>
#include <vector>

#include "matchState.hpp"
#include "vec2.hpp"

namespace ElyverseFootball::SimMatch {

// What a carrier without a valid option does about it
// (docs/on-ball.md#pass-decisions): where he looks for one, and where he kicks
// the ball when there is none. Pure functions of the state; the pass decision
// (passDecision.hpp) drives them.

// The scan of the player at this index while it is still his: he owns the
// ball and has not touched it since the scan began but to carry it on in his
// active run (activeCarry(), carrying.hpp). nullptr otherwise.
[[nodiscard]] const CarrierScan* activeScan(const MatchState& state, std::size_t playerIndex);

// Where the player at this index looks, which decides what he sees
// (docs/players.md#perception): while his ball rolls ahead of him after a touch
// along his active run's gaze (docs/on-ball.md#ball-carrying), otherwise along
// his active scan's gaze, otherwise along his facing.
[[nodiscard]] SimCore::Vec2 lookDirection(const MatchState& state, std::size_t playerIndex);

// A point a carrier looks at, and whether it lies on the side he looks to
// first.
struct LookTarget {
  SimCore::Vec2 point;
  bool firstSide = true;

  friend bool operator==(const LookTarget&, const LookTarget&) = default;
};

// The points the carrier at this index looks at in turn, at most two. Sides
// are taken from his facing: the first side is the one his turn toward the
// opponent's goal is shorter on -- toward the pitch's middle if he faces the
// goal or straight away from it --, the other side the one behind it. A
// forward (his slot holds runInBehind) first looks at the opponent's penalty
// spot, everyone else at the centroid of the movement targets of his
// teammates on the first side -- where their tactic wants them, or where they
// stand without a target. Then at the centroid of those on the other side. A
// side without a teammate gives no point.
[[nodiscard]] std::vector<LookTarget> lookTargets(const MatchState& state,
                                                  std::size_t carrierIndex);

// How far a carrier without any option can kick the ball, how far he wants
// to, and where it should land.
struct ClearanceRange {
  // His longest kick.
  double reach = 0.0;  // m
  // A clearance shorter than this is no clearance.
  double minDistance = 0.0;  // m
  // It lands this far inside a touchline, and no closer than goalLineMargin
  // to the opponent's goal line.
  double touchlineMargin = 0.0;  // m
  double goalLineMargin = 0.0;   // m
};

// Where a carrier without any option kicks the ball: touchlineMargin inside
// a touchline, as far up the pitch as reach takes it from him but no closer
// than goalLineMargin to the opponent's goal line -- on the side where his
// team's pitch control is higher, the side nearer to him on a tie or without
// a grid. A side whose point lies closer to the ball than minDistance gives
// way to the other; if both do, the farther. Never farther from the ball than
// reach: if the point lies beyond it, the one at reach on the way there.
[[nodiscard]] SimCore::Vec2 clearanceTarget(const MatchState& state, std::size_t carrierIndex,
                                            const ClearanceRange& range);

}  // namespace ElyverseFootball::SimMatch
