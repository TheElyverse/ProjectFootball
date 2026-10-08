#pragma once

#include <cstddef>
#include <optional>
#include <string_view>
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

// Throws std::invalid_argument unless the confidence and completion limits
// lie in [0, 1], the pass distances are finite and not negative with the
// maximum above the minimum, the margin and pressure radius are positive and
// finite, the weights are finite, not negative and at most
// kMaxScoringWeight, and the lofted bias at most that far either side of 0. Scores then stay
// finite, so candidates sort and the softmax works.
inline constexpr double kMaxScoringWeight = 1e6;
void validate(const PassScoringConfig& config);

// The weighted parts of a candidate's utility (docs/pass-candidates.md):
// completion and progression add, receiver pressure and interception risk
// subtract, so those two are 0 or negative, and directness is the lofted
// bias of a lofted pass, 0 for a ground pass. Their sum is the utility, bit
// for bit. Kept apart so a decision can be explained.
struct PassContributions {
  double completion = 0.0;
  double progression = 0.0;
  double pressure = 0.0;
  double risk = 0.0;
  double directness = 0.0;

  [[nodiscard]] double total() const noexcept {
    return completion + progression + pressure + risk + directness;
  }

  friend bool operator==(const PassContributions&, const PassContributions&) = default;
};

[[nodiscard]] PassContributions passContributions(const PassCandidate& candidate,
                                                  const PassScoringConfig& scoring) noexcept;

// The part that contributed most to a utility, by absolute value:
// "completion", "progression", "pressure", "risk" or "directness". Ties go to
// the one listed first.
[[nodiscard]] std::string_view dominantContribution(const PassContributions& parts) noexcept;

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
// Ordered by orderPassCandidates(). Deterministic: the same state gives the
// same list.
[[nodiscard]] std::vector<PassCandidate> generatePassCandidates(const MatchState& state,
                                                                std::size_t carrierIndex,
                                                                SimCore::SimTick now,
                                                                double secondsPerTick,
                                                                const PassCandidateRules& rules);

// Scores a candidate whose receiver, target, distance, receiverConfidence
// and lofted are set, its pass played from `from` by a player of `side`:
// `arrives` is the chance that no remembered opponent gets to the ball first,
// and nearestOpponent the distance of the opponent nearest the target. Sets
// interceptionRisk, completion, progression, receiverPressure and utility.
void scorePassCandidate(PassCandidate& candidate, SimCore::Vec2 from, TeamSide side, double arrives,
                        double nearestOpponent, const Pitch& pitch,
                        const PassScoringConfig& scoring) noexcept;

// Puts valid candidates first by descending utility, then the rest; ties by
// receiver id, a ground pass before a lofted one to the same receiver.
void orderPassCandidates(std::vector<PassCandidate>& candidates);

// The direction along x a side attacks: +1 for home, -1 for away. Fixed in
// the sandbox, which has no halves and in which every scenario places home at
// x = 0; the one place to change once rules let teams switch ends.
[[nodiscard]] double attackingDirection(TeamSide side) noexcept;

}  // namespace ElyverseFootball::SimMatch
