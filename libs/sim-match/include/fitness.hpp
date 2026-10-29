#pragma once

#include <cstdint>

#include "matchState.hpp"

namespace ElyverseFootball::SimMatch {

// How players tire (docs/players.md#fitness). Part of MatchConfig and of every
// replay.
struct FitnessConfig {
  // Running faster than this share of his maxSpeed is a sprint.
  double sprintSpeedShare = 0.85;
  // The real seconds of sprinting a full reserve lasts, and the real seconds
  // below sprint speed an empty one takes to fill again.
  double sprintSeconds = 15.0;    // s
  double recoverySeconds = 45.0;  // s
  // The share of his maxSpeed a player has lost after the whole duration of a
  // match on the pitch; linear in the share he has played.
  double fullMatchSpeedLoss = 0.06;

  friend bool operator==(const FitnessConfig&, const FitnessConfig&) = default;
};

// Throws std::invalid_argument for a sprint share outside (0, 1], durations
// that are not positive and finite, or a speed loss outside [0, 1).
void validate(const FitnessConfig& config);

// How long a tick lasts, and the ticks of a half's regulation time
// (halfTicks()).
struct FitnessTime {
  double secondsPerTick = 0.0;
  std::int64_t halfTicks = 1;
};

// A player's fitness after one more tick on the pitch at the speed he ran in
// the tick before, his velocity: one more played tick; the sprint reserve
// drained by the tick's seconds over sprintSeconds while he ran faster than
// sprintSpeedShare of his maxSpeed, refilled by them over recoverySeconds
// otherwise; and the top speed share the two leave him -- 1 less
// fullMatchSpeedLoss times the share of the match he has played, and no more
// than sprintSpeedShare while his reserve is empty.
[[nodiscard]] PlayerFitness stepFitness(const PlayerMatchState& player, const FitnessTime& time,
                                        const FitnessConfig& config) noexcept;

}  // namespace ElyverseFootball::SimMatch
