#pragma once

#include <cstddef>
#include <string_view>
#include <vector>

#include "ballPhysics.hpp"
#include "crossCandidate.hpp"
#include "matchState.hpp"
#include "passing.hpp"
#include "perception.hpp"
#include "reception.hpp"
#include "shotCandidate.hpp"
#include "simTime.hpp"

namespace ElyverseFootball::SimMatch {

// Throws std::invalid_argument unless the confidence and win chance lie in
// [0, 1], the depth share and the curl share in [0, 1], the speeds, the
// header height and the margin are positive and finite, and the weights
// finite, not negative and at most kMaxScoringWeight (passCandidates.hpp).
void validate(const CrossScoringConfig& config);

// The weighted parts of a cross's utility (docs/on-ball.md#crossing): winning
// it and the threat from there add, losing it subtracts, so that one is 0 or
// negative. Their sum is the utility, bit for bit.
struct CrossContributions {
  double win = 0.0;
  double threat = 0.0;
  double loss = 0.0;

  [[nodiscard]] double total() const noexcept { return win + threat + loss; }

  friend bool operator==(const CrossContributions&, const CrossContributions&) = default;
};

[[nodiscard]] CrossContributions crossContributions(const CrossCandidate& candidate,
                                                    const CrossScoringConfig& scoring) noexcept;

// The part with the largest absolute value, the reason a decision trace
// gives: "win", "threat" or "loss". Ties go to the one listed first.
[[nodiscard]] std::string_view dominantCrossContribution(const CrossContributions& parts) noexcept;

// Cross scoring adjusted to how much risk his tactic accepts in its current
// phase, passingRisk in [0, 1]: the threat counts (0.5 + risk) times as
// much, losing the ball (1.5 - risk) times, and the win chance a cross needs
// (1.3 - 0.6 risk) times, at most 1. At 0.5 nothing changes.
[[nodiscard]] CrossScoringConfig crossScoringForRisk(const CrossScoringConfig& scoring,
                                                     double passingRisk) noexcept;

// What cross generation needs to know besides the state: his shot's curl
// and launch slope come from the shot scoring.
struct CrossCandidateRules {
  CrossScoringConfig scoring;
  ShotScoringConfig shooting;
  BallPhysics ball;
  PassConfig passing;
  PerceptionConfig perception;
  ReceptionConfig reception;
};

// The crosses of the player on the ball at carrierIndex
// (docs/on-ball.md#crossing): none unless crossing is enabled and the ball
// is wide in the final third -- within maxDepthShare of the pitch length of
// the goal line he attacks, outside the width of the penalty area. Then one
// candidate per area of the box -- near post, penalty spot, far post in the
// air, the cut-back along the ground -- and one per teammate whose movement
// target lies in the penalty area, scored only from his memory: who of his
// remembered teammates gets there first, against when his remembered
// opponents and the keeper do, and how dangerous it is from there. Ordered
// valid candidates first by descending utility, then the rest; ties by
// area, then runner id. Deterministic: the same state gives the same list.
[[nodiscard]] std::vector<CrossCandidate> generateCrossCandidates(const MatchState& state,
                                                                  std::size_t carrierIndex,
                                                                  SimCore::StepTime time,
                                                                  const CrossCandidateRules& rules);

}  // namespace ElyverseFootball::SimMatch
