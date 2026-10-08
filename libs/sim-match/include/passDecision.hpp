#pragma once

#include <cstddef>
#include <optional>
#include <span>
#include <string_view>

#include "distribution.hpp"
#include "matchSimulation.hpp"
#include "passCandidates.hpp"
#include "random.hpp"
#include "shotCandidates.hpp"

namespace ElyverseFootball::SimMatch {

// How players on the ball decide; see docs/pass-decisions.md. Part of
// MatchConfig and therefore of every replay.
struct DecisionConfig {
  // 6 ticks at 30 Hz: a player on the ball decides five times a second.
  int intervalTicks = 6;
  // A player keeps a ball he has just taken at least this long.
  double minHoldSeconds = 0.5;
  // Softmax temperature over utility: lower picks the best option more often,
  // higher spreads the choice.
  double temperature = 0.15;
  PassScoringConfig scoring;
  ShotScoringConfig shooting;
  // How the goalkeeper distributes the ball (docs/goalkeeper-distribution.md).
  DistributionConfig distribution;

  friend bool operator==(const DecisionConfig&, const DecisionConfig&) = default;
};

// What the player on the ball chose: to pass or to shoot -- kPassed or kShot
// -- and which, by its index in the list it comes from.
struct OnBallChoice {
  DecisionOutcome outcome = DecisionOutcome::kPassed;
  std::size_t index = 0;

  friend bool operator==(const OnBallChoice&, const OnBallChoice&) = default;
};

// Picks one of the valid passes at the front of a list ordered like
// generatePassCandidates()'s or the first shot of a list ordered like
// generateShotCandidates()'s, if it is valid -- the best shot competes, not
// every zone of the goal --, with probability proportional to
// exp(utility / temperature): the seeded softmax policy. Draws exactly one
// number from random if there is a valid option and none otherwise; empty
// without a valid option. The exponential is stableExp(), so the choice is
// the same on every platform.
[[nodiscard]] std::optional<OnBallChoice> chooseOnBall(std::span<const PassCandidate> passes,
                                                       std::span<const ShotCandidate> shots,
                                                       double temperature,
                                                       SimCore::RandomNumberGenerator& random);

// Scoring adjusted to how much risk a carrier's tactic accepts, passingRisk
// in [0, 1] (docs/pass-decisions.md): progression counts (0.5 + risk) times
// as much, interception risk (1.5 - risk) times, and the completion a pass
// needs to be offered (1.3 - 0.6 risk) times, at most 1. At 0.5 -- the
// reference tactic's -- nothing changes.
[[nodiscard]] PassScoringConfig scoringForRisk(const PassScoringConfig& scoring,
                                               double passingRisk) noexcept;

// Shot scoring adjusted to the same passingRisk (docs/shot-decisions.md): a
// goal counts (0.5 + risk) times as much, losing the ball (1.5 - risk) times,
// and the goal chance a shot needs to be offered (1.3 - 0.6 risk) times, at
// most 1. At 0.5 nothing changes.
[[nodiscard]] ShotScoringConfig shotScoringForRisk(const ShotScoringConfig& scoring,
                                                   double passingRisk) noexcept;

inline constexpr std::string_view kPassDecisionSystemName = "pass decision";

// Every config.intervalTicks ticks, the player on the ball decides:
//
//   1. Nothing to decide if the ball is free or its owner already has an
//      action pending.
//   2. He keeps a ball he took less than minHoldSeconds ago, and one he
//      holds in his hands less than the distribution's holdSeconds.
//   3. He lists and scores his options with generatePassCandidates() and
//      generateShotCandidates(), from his perception only -- scored for the
//      passing risk of his side's tactic in its current phase
//      (scoringForRisk(), shotScoringForRisk()), if it has one. A
//      goalkeeper adds his long balls (generateLongBallCandidates()), biased
//      by his tactic's directness (directnessBias()), and from his hands
//      throws no farther than the distribution's throwRange
//      (docs/goalkeeper-distribution.md).
//   4. He picks one of the valid passes and the best valid shot with
//      chooseByUtility(), drawing from the kAi random stream, and writes it
//      as his pending action -- the ball system plays it in the next step.
//      Without a valid option he keeps the ball.
//
// Writes the carrier's pending action only. Throws std::invalid_argument for
// an interval below one tick, a negative or non-finite hold time, a
// temperature that is not positive and finite, or invalid scoring or
// distribution (validate(PassScoringConfig), validate(ShotScoringConfig),
// validate(DistributionConfig)).
[[nodiscard]] MatchSystem makePassDecisionSystem(const DecisionConfig& config,
                                                 const PassCandidateRules& rules);

}  // namespace ElyverseFootball::SimMatch
