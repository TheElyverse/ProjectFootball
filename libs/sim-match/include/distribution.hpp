#pragma once

#include <cstddef>
#include <vector>

#include "matchState.hpp"
#include "passCandidate.hpp"
#include "passCandidates.hpp"
#include "simTime.hpp"

namespace ElyverseFootball::SimMatch {

// How a goalkeeper distributes the ball; see docs/goalkeeper-distribution.md.
// Part of DecisionConfig and therefore of every replay.
struct DistributionConfig {
  // With the ball in his hands he keeps it at least this long, for his side
  // to get into shape.
  double holdSeconds = 2.0;  // s
  // From his hands his passes are throws, rolled out along the ground, and
  // he throws no farther than this.
  double throwRange = 25.0;  // m
  // A long ball goes at least this far.
  double minLongDistance = 25.0;  // m
  // How long a long ball is in the air before it comes down at its target.
  double flightSeconds = 3.0;  // s
  // An opponent's margin on a long ball is the time he has to spare reaching
  // where it comes down, negative if he gets there before the receiver. The
  // risk from him falls smoothly from 1 at minus this many seconds through
  // 1/2 at zero to 0 at plus this.
  double contestMarginSeconds = 1.0;  // s
  // What a long ball is worth on top of its scores at directness 1, and
  // costs at directness 0 (directnessBias()).
  double directnessWeight = 0.5;

  friend bool operator==(const DistributionConfig&, const DistributionConfig&) = default;
};

// Throws std::invalid_argument unless every value is finite, the hold and
// the shortest long ball are not negative, the throw range, the flight time
// and the margin are positive, and the directness weight is not negative and
// at most kMaxScoringWeight.
void validate(const DistributionConfig& config);

// The lofted bias of a keeper whose tactic has this directness, in [0, 1]:
// directnessWeight · (2 · directness − 1). Nothing at 0.5.
[[nodiscard]] double directnessBias(double directness, const DistributionConfig& config) noexcept;

// The goalkeeper's long balls (docs/goalkeeper-distribution.md): every
// teammate the carrier at carrierIndex remembers with at least minConfidence
// becomes a lofted candidate, aimed at where he believes the teammate is now
// and kicked at the speed along the ground that brings it down there after
// flightSeconds (flightSpeed()). Its interception risk is the chance that a
// remembered opponent wins it where it comes down: an opponent's margin is
// how much later than the receiver he gets there, each counted from no
// earlier than the ball, so two who are both there in time are even. The
// scores are those of a ground pass otherwise (scorePassCandidate()), with
// the scoring's loftedBias on top.
//
// A long ball shorter than minLongDistance is kTooClose, one that needs more
// than the passing's maxLoftedSpeed kOutOfReach, and one less likely than
// minCompletion kUnlikely. Ordered by orderPassCandidates(); deterministic.
[[nodiscard]] std::vector<PassCandidate> generateLongBallCandidates(
    const MatchState& state, std::size_t carrierIndex, SimCore::SimTick now, double secondsPerTick,
    const PassCandidateRules& rules, const DistributionConfig& config);

}  // namespace ElyverseFootball::SimMatch
