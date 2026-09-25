#pragma once

#include <string_view>

#include "desiredRegion.hpp"
#include "matchSimulation.hpp"
#include "offBallActions.hpp"
#include "perception.hpp"

namespace ElyverseFootball::SimMatch {

// What the tactical movement system needs to know besides the state.
struct TacticalMovementRules {
  PositioningConfig positioning;
  OffBallConfig offBall;
  PerceptionConfig perception;
};

inline constexpr std::string_view kTacticalMovementSystemName = "tactical movement";

// Moves the players of every side with a tactic and a phase, every
// positioning.intervalTicks ticks (docs/desired-region.md,
// docs/off-ball-movement.md). Each player but the one on the ball and the
// side's chaser:
//
//   1. gets a new desired region (chooseDesiredRegion());
//   2. with his team on the ball, decides an off-ball action when one is
//      due (isOffBallDecisionDue()): generateOffBallCandidates(), then the
//      seeded softmax chooseByUtility() with a draw from the kAi stream --
//      the same pipeline as a pass -- and keeps it until the next decision;
//   3. runs to the action's target, or to his region's centre while he
//      holds position or his team does not have the ball.
//
// Writes movement targets and tactical states only. Throws
// std::invalid_argument for an invalid configuration.
[[nodiscard]] MatchSystem makeTacticalMovementSystem(const TacticalMovementRules& rules);

}  // namespace ElyverseFootball::SimMatch
