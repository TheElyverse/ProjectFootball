#pragma once

#include <cstddef>
#include <vector>

#include "actionCandidate.hpp"
#include "desiredRegion.hpp"
#include "matchState.hpp"
#include "perception.hpp"
#include "simTime.hpp"

namespace ElyverseFootball::SimMatch {

// How players of a side without the ball decide what to do; see
// docs/team-tactics.md#defensive-shape. Part of MatchConfig and therefore of
// every replay. The decision cadence is OffBallConfig's.
struct DefensiveConfig {
  // An opponent this close to a player's desired region is in his zone.
  double markRadius = 12.0;  // m
  // A marker stands this far goal-side of his man.
  double markDistance = 1.5;  // m
  // A runner this close to the player can be tracked.
  double trackRadius = 15.0;  // m
  // An opponent running at least this fast toward the goal is a runner.
  double runnerSpeed = 3.0;  // m/s
  // A tracker aims where the runner will be this much later.
  double trackLeadSeconds = 0.5;
  // A coverer stands this far behind the teammate he covers, toward the goal.
  double coverDistance = 6.0;  // m
  // A player this close to the carrier can press him, and one this close to
  // a lane can block it.
  double pressRadius = 15.0;  // m
  // A presser aims this far from the carrier, on the line to his option.
  double pressDistance = 1.0;  // m
  // A carrier who has stood on the ball this long since his last touch is
  // pressed by the nearest outfield player within idlePressRadius of him,
  // whatever the phase's pressing.
  double idleSeconds = 2.0;
  double idlePressRadius = 3.0;  // m
  // A lane blocker stands at least this far from the carrier and the
  // receiver.
  double laneMinDistance = 2.0;  // m
  // The distance that costs one unit of effort.
  double effortScale = 20.0;  // m
  // How strongly holding the block counts as a duty on its own, in [0, 1].
  double holdResponsibility = 0.4;
  // Softmax temperature of the choice.
  double temperature = 0.2;
  // Weights of the utility's parts.
  double responsibilityWeight = 1.0;
  double regionWeight = 0.5;
  double spaceWeight = 0.4;
  double urgencyWeight = 0.8;
  double effortWeight = 0.3;
  // Near his own goal a defender picks up the men about the ball -- marks,
  // tracks and covers -- rather than hold his place
  // (docs/team-tactics.md#defensive-shape): with the ball
  // and his region's centre within dangerShare of the pitch's length of it
  // (50 m on the standard pitch), the urgency weighs up to 1 + dangerUrgency
  // times as much and the region up to 1 - dangerRegionRelief as much, the
  // more the nearer both. Farther out the shape holds as the tactic asks, and
  // a forward stays up.
  double dangerShare = 0.48;
  double dangerUrgency = 4.0;
  double dangerRegionRelief = 0.9;

  friend bool operator==(const DefensiveConfig&, const DefensiveConfig&) = default;
};

// Throws std::invalid_argument for a radius, distance, speed or temperature
// that is not positive and finite, a negative lead or idle time, a hold
// responsibility, danger share or danger region relief outside [0, 1] or a
// negative or non-finite weight.
void validate(const DefensiveConfig& config);

// What defensive decisions need to know besides the state and the player.
struct DefensiveRules {
  SimCore::StepTime time;
  const DefensiveConfig* config = nullptr;
  const PositioningConfig* positioning = nullptr;
  const PerceptionConfig* perception = nullptr;
};

// The options of the player at this index, whose team does not have the
// ball, with region his desired region: holding the block; marking the
// nearest opponent in his zone -- within markRadius of his region -- goal-side;
// tracking the most dangerous runner he has seen; covering -- behind a
// teammate who presses the carrier, otherwise behind the teammate nearest the
// ball; pressing the carrier on the line to his nearest option
// (pressTarget()); and blocking the lane to the carrier's option nearest to
// him (laneBlockTarget()). Opponents come from his memory only, so a run he
// has not seen is not tracked and a carrier he does not remember is not
// pressed. In that fixed order, each only if its subject exists.
// Deterministic.
[[nodiscard]] std::vector<ActionCandidate> generateDefensiveCandidates(const MatchState& state,
                                                                       std::size_t playerIndex,
                                                                       const DesiredRegion& region,
                                                                       const DefensiveRules& rules);

}  // namespace ElyverseFootball::SimMatch
