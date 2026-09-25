#pragma once

#include <string_view>

#include "matchSimulation.hpp"

namespace ElyverseFootball::SimMatch {

// How a presser tries to win the ball; see docs/pressing.md. A placeholder
// for the duels of the P3 baseline: one seeded roll per attempt, so pressing
// can win the ball at all. Part of MatchConfig and therefore of every replay.
struct ChallengeConfig {
  // 3 ticks at 30 Hz: pressers within reach are checked ten times a second.
  int intervalTicks = 3;
  // A presser this close to the carrier can challenge him.
  double radius = 1.2;  // m
  // The chance that one challenge wins the ball.
  double winChance = 0.2;
  // A player challenges at most once this often.
  double attemptSeconds = 0.5;
  // A carrier who has had the ball for less than this cannot be challenged:
  // he is still settling it, and a winner is not robbed straight back.
  double protectSeconds = 0.5;

  friend bool operator==(const ChallengeConfig&, const ChallengeConfig&) = default;
};

inline constexpr std::string_view kChallengeSystemName = "ball challenge";

// Every config.intervalTicks ticks, each player whose decided action is to
// press the carrier (ActionType::kPressCarrier), who stands within radius of
// him and has not challenged for attemptSeconds, challenges once, in player
// order: one draw from the kExecution stream, won below winChance. A carrier
// who has had the ball for less than protectSeconds is not challenged. The
// first won challenge takes the ball: the presser owns it and is its last
// touch, and the step records BallWon and PossessionChanged. Writes the
// ball's owner and last touch and players' last challenge, nothing else.
// Throws std::invalid_argument for an interval below one tick, a radius or
// attempt time that is not positive and finite, a win chance outside [0, 1]
// or a negative protection.
[[nodiscard]] MatchSystem makeChallengeSystem(const ChallengeConfig& config);

}  // namespace ElyverseFootball::SimMatch
