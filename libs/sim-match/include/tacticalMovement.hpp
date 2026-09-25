#pragma once

#include <string_view>

#include "defensiveActions.hpp"
#include "desiredRegion.hpp"
#include "matchSimulation.hpp"
#include "offBallActions.hpp"
#include "perception.hpp"

namespace ElyverseFootball::SimMatch {

// What the tactical movement system needs to know besides the state.
struct TacticalMovementRules {
  PositioningConfig positioning;
  OffBallConfig offBall;
  DefensiveConfig defensive;
  PerceptionConfig perception;
};

inline constexpr std::string_view kTacticalMovementSystemName = "tactical movement";

// Moves the players of every side with a tactic and a phase, every
// positioning.intervalTicks ticks (docs/desired-region.md,
// docs/off-ball-movement.md, docs/defensive-shape.md). Each player but the
// one on the ball and the side's chaser:
//
//   1. gets a new desired region (chooseDesiredRegion());
//   2. with a role in his side's press (MatchState::press()), takes that
//      role's action -- the team assigned it, he does not choose;
//      otherwise, unless he is the goalkeeper, decides an action when one is due
//      (isActionDecisionDue()): with his team on the ball from
//      generateOffBallCandidates(), without it from
//      generateDefensiveCandidates(), then the seeded softmax
//      chooseByUtility() with a draw from the kAi stream -- the same
//      pipeline as a pass -- and keeps it until the next decision;
//   3. runs to the action's target, or to his region's centre while he
//      holds position.
//
// Writes movement targets and tactical states only. Throws
// std::invalid_argument for an invalid configuration.
[[nodiscard]] MatchSystem makeTacticalMovementSystem(const TacticalMovementRules& rules);

}  // namespace ElyverseFootball::SimMatch
