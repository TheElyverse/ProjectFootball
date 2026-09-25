#pragma once

#include <cstddef>
#include <vector>

#include "actionCandidate.hpp"
#include "desiredRegion.hpp"
#include "matchState.hpp"
#include "perception.hpp"
#include "simTime.hpp"
#include "tacticalState.hpp"
#include "vec2.hpp"

namespace ElyverseFootball::SimMatch {

// How teammates of the ball carrier decide what to do without the ball; see
// docs/off-ball-movement.md. Part of MatchConfig and therefore of every
// replay.
struct OffBallConfig {
  // Players this close to the ball decide every nearIntervalTicks, the rest
  // every farIntervalTicks: relevance decides how often a player thinks.
  double nearBallRadius = 20.0;  // m
  int nearIntervalTicks = 6;
  int farIntervalTicks = 18;
  // A support position lies this far from the carrier.
  double supportDistance = 10.0;  // m
  // Space is looked for up to this far from the player.
  double spaceSearchRadius = 12.0;  // m
  // A run in behind aims this far past the opponent's defensive line.
  double runDepth = 6.0;  // m

  // An opponent this close to a passing lane closes it completely.
  double laneRadius = 4.0;  // m
  // An opponent this close to the carrier presses him: the closer, the more
  // urgent support becomes.
  double pressedRadius = 6.0;  // m
  // The distance that costs one unit of effort.
  double effortScale = 20.0;  // m
  // How strongly holding position counts as a duty on its own, in [0, 1].
  double holdResponsibility = 0.4;
  // Softmax temperature of the choice.
  double temperature = 0.2;
  // Weights of the utility's parts.
  double responsibilityWeight = 1.0;
  double regionWeight = 0.5;
  double spaceWeight = 0.6;
  double laneWeight = 0.8;
  double urgencyWeight = 0.6;
  double effortWeight = 0.3;

  friend bool operator==(const OffBallConfig&, const OffBallConfig&) = default;
};

// Throws std::invalid_argument for a radius, distance or temperature that is
// not positive and finite, an interval below one tick, a hold
// responsibility outside [0, 1] or a negative or non-finite weight.
void validate(const OffBallConfig& config);

// How open the passing lane from `from` to `until` is, in [0, 1]: the distance
// of the nearest opponent to the segment between them, over laneRadius,
// capped at 1. 1 without opponents.
[[nodiscard]] double laneOpenness(SimCore::Vec2 from, SimCore::Vec2 until,
                                  const std::vector<RememberedPlayer>& remembered,
                                  double laneRadius) noexcept;

// The options of the player at this index, a teammate of the carrier, with
// region his current desired region: holding it, supporting the carrier,
// moving into space, running in behind the opponent's line -- if he
// remembers enough of it to see a line -- creating width and occupying the
// halfspace on his side of the pitch. Scored from his own memory, his
// team's pitch control and his tactic (docs/off-ball-movement.md), in that
// fixed order. Deterministic.
[[nodiscard]] std::vector<ActionCandidate> generateOffBallCandidates(
    const MatchState& state, std::size_t playerIndex, const DesiredRegion& region,
    SimCore::SimTick now, double secondsPerTick, const OffBallConfig& config,
    const PositioningConfig& positioning, const PerceptionConfig& perception);

// Whether the player at this index decides again at tick now: he has no
// off-ball action yet, or he decided nearIntervalTicks ago near the ball,
// farIntervalTicks ago away from it.
[[nodiscard]] bool isOffBallDecisionDue(const MatchState& state, std::size_t playerIndex,
                                        SimCore::SimTick now, const OffBallConfig& config);

}  // namespace ElyverseFootball::SimMatch
