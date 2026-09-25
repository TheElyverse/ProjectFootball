#pragma once

#include <cstddef>
#include <optional>
#include <vector>

#include "ballMovement.hpp"
#include "ids.hpp"
#include "matchState.hpp"
#include "passCandidate.hpp"
#include "passing.hpp"
#include "perception.hpp"
#include "reception.hpp"
#include "simTime.hpp"
#include "vec2.hpp"

namespace ElyverseFootball::SimMatch {

// How a player on the ball weighs his passing options; see
// docs/pass-candidates.md.
struct PassScoringConfig {
  // Teammates and opponents remembered with less confidence are ignored.
  double minConfidence = 0.3;
  double minPassDistance = 2.0;   // m
  double maxPassDistance = 35.0;  // m
  // An opponent's margin is the time he has to spare reaching the pass,
  // negative if he gets there first. The risk from him falls smoothly from
  // 1 at minus this many seconds through 1/2 at zero to 0 at plus this.
  double interceptionMarginSeconds = 0.6;
  // An opponent this close to the receiver puts no pressure on him at the
  // edge and full pressure at zero distance.
  double pressureRadius = 6.0;  // m
  // Candidates less likely to arrive are not offered to the decision.
  double minCompletion = 0.35;
  // Utility = completion·w_c + progression·w_p − pressure·w_r − risk·w_i.
  double completionWeight = 1.0;
  double progressionWeight = 0.8;
  double pressureWeight = 0.3;
  double riskWeight = 0.3;

  friend bool operator==(const PassScoringConfig&, const PassScoringConfig&) = default;
};

// Throws std::invalid_argument unless the confidence and completion limits
// lie in [0, 1], the pass distances are finite and not negative with the
// maximum above the minimum, the margin and pressure radius are positive and
// finite, and the weights are finite, not negative and at most
// kMaxScoringWeight. Scores then stay finite, so candidates sort and the
// softmax works.
inline constexpr double kMaxScoringWeight = 1e6;
void validate(const PassScoringConfig& config);

// What candidate generation needs to know besides the state.
struct PassCandidateRules {
  PassScoringConfig scoring;
  BallPhysics ball;
  PassConfig passing;
  PerceptionConfig perception;
  ReceptionConfig reception;
};

// Every teammate the carrier at carrierIndex remembers with at least
// minConfidence becomes a candidate, scored only from his memory: the
// receiver's estimated position, and the estimated positions and velocities
// of the opponents he remembers. Opponents he does not remember do not
// count, however close they really are. Squad membership -- who is a
// teammate -- is known; positions are not.
//
// Ordered valid candidates first by descending utility, then the rest; ties
// by receiver id. Deterministic: the same state gives the same list.
[[nodiscard]] std::vector<PassCandidate> generatePassCandidates(const MatchState& state,
                                                                std::size_t carrierIndex,
                                                                SimCore::SimTick now,
                                                                double secondsPerTick,
                                                                const PassCandidateRules& rules);

// The direction along x a side attacks: +1 for home, -1 for away. Fixed in
// the sandbox, which has no halves and in which every scenario places home at
// x = 0; the one place to change once rules let teams switch ends.
[[nodiscard]] double attackingDirection(TeamSide side) noexcept;

}  // namespace ElyverseFootball::SimMatch
