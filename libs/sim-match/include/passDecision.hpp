#pragma once

#include <cstddef>
#include <optional>
#include <span>
#include <string_view>

#include "matchSimulation.hpp"
#include "passCandidates.hpp"
#include "random.hpp"

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

  friend bool operator==(const DecisionConfig&, const DecisionConfig&) = default;
};

// Picks one of the valid candidates at the front of a list ordered like
// generatePassCandidates()'s, with probability proportional to
// exp(utility / temperature): the seeded softmax policy. Draws exactly one
// number from random if there is a valid candidate and none otherwise;
// empty without a valid candidate. The exponential is stableExp(), so the
// choice is the same on every platform.
[[nodiscard]] std::optional<std::size_t> choosePass(std::span<const PassCandidate> candidates,
                                                    double temperature,
                                                    SimCore::RandomNumberGenerator& random);

inline constexpr std::string_view kPassDecisionSystemName = "pass decision";

// Every config.intervalTicks ticks, the player on the ball decides:
//
//   1. Nothing to decide if the ball is free or a pass is already pending.
//   2. He keeps a ball he took less than minHoldSeconds ago.
//   3. He lists and scores his options with generatePassCandidates(), from
//      his perception only.
//   4. He picks one with choosePass(), drawing from the kAi random stream, and
//      writes it as the pending pass -- the ball system plays it in the next
//      step. Without a valid option he keeps the ball.
//
// Writes the pending pass only. Throws std::invalid_argument for an interval
// below one tick, a negative or non-finite hold time, or a temperature that
// is not positive and finite.
[[nodiscard]] MatchSystem makePassDecisionSystem(const DecisionConfig& config,
                                                 const PassCandidateRules& rules);

}  // namespace ElyverseFootball::SimMatch
