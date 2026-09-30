#pragma once

#include <cstddef>
#include <string_view>
#include <vector>

#include "ballPhysics.hpp"
#include "matchState.hpp"
#include "perception.hpp"
#include "pitch.hpp"
#include "reception.hpp"
#include "shotCandidate.hpp"
#include "simTime.hpp"

namespace ElyverseFootball::SimMatch {

// Throws std::invalid_argument unless the confidence, opening, share and
// chance limits lie in [0, 1] -- the opening in [0, 2] --, the grid has at
// least two columns and one row and at most kMaxShotZones on either side,
// the distances, speed, reaches, margin, spreads and radius are positive
// and finite, the pressure spread and frame margin finite and not negative,
// and the weights finite, not negative and at most kMaxScoringWeight
// (passCandidates.hpp). Scores then stay finite, so candidates sort and the
// softmax works.
inline constexpr int kMaxShotZones = 64;
void validate(const ShotScoringConfig& config);

// The weighted parts of a shot's utility (docs/shot-decisions.md): the goal
// and the second ball add, losing the ball subtracts, so that one is 0 or
// negative. Their sum is the utility, bit for bit. Kept apart so a decision
// can be explained.
struct ShotContributions {
  double goal = 0.0;
  double secondBall = 0.0;
  double possession = 0.0;

  [[nodiscard]] double total() const noexcept { return goal + secondBall + possession; }

  friend bool operator==(const ShotContributions&, const ShotContributions&) = default;
};

[[nodiscard]] ShotContributions shotContributions(const ShotCandidate& candidate,
                                                  const ShotScoringConfig& scoring) noexcept;

// The part that contributed most to a utility, by absolute value: "goal",
// "secondBall" or "possession". Ties go to the one listed first.
[[nodiscard]] std::string_view dominantShotContribution(const ShotContributions& parts) noexcept;

// The goal a side attacks: the one at the end attackingDirection() points to.
[[nodiscard]] Goal attackedGoal(const Pitch& pitch, TeamSide side) noexcept;

// How wide a goal looks from a point: the distance between the directions to
// its two posts as unit vectors, 2·sin(angle / 2), in [0, 2]; about the angle
// in radians for a narrow one. No trigonometry, so it is the same on every
// platform. 0 from a post itself.
[[nodiscard]] double goalOpening(SimCore::Vec2 from, const Goal& goal) noexcept;

// What shot generation needs to know besides the state.
struct ShotCandidateRules {
  ShotScoringConfig scoring;
  BallPhysics ball;
  PerceptionConfig perception;
  ReceptionConfig reception;
};

// One candidate per zone of the aiming grid over the goal the shooter at
// shooterIndex attacks, scored only from his memory: the opponents he
// remembers with at least minConfidence, at their estimated positions and
// velocities. His side's goalkeeper -- by the tactic's slot, which the squad
// knows -- saves, everyone else blocks; an opponent he does not remember
// does neither. His own accuracy he knows.
//
// A shot from too far or at too narrow a goal is not scored: every zone
// carries that rejection. Ordered valid candidates first by descending
// utility, then the rest; ties by column, then row. Deterministic: the same
// state gives the same list.
[[nodiscard]] std::vector<ShotCandidate> generateShotCandidates(const MatchState& state,
                                                                std::size_t shooterIndex,
                                                                SimCore::SimTick now,
                                                                double secondsPerTick,
                                                                const ShotCandidateRules& rules);

}  // namespace ElyverseFootball::SimMatch
