#pragma once

#include <cstdint>
#include <string_view>

#include "matchSimulation.hpp"

namespace ElyverseFootball::SimMatch {

// How a coach reaches a running match (docs/match-engine.md#coaching). Part of
// MatchConfig and of every replay.
struct CoachingConfig {
  // Real seconds a tactic takes from the touchline to the players, whatever
  // the match's duration.
  double instructionDelaySeconds = 5.0;  // s
  // How many substitutions a side may make in a match.
  int maxSubstitutions = 5;

  friend bool operator==(const CoachingConfig&, const CoachingConfig&) = default;
};

inline constexpr double kMaxInstructionDelaySeconds = 600.0;
inline constexpr int kMaxSubstitutions = 11;

// Throws std::invalid_argument for a negative or non-finite delay or one over
// kMaxInstructionDelaySeconds, or a number of substitutions outside
// [0, kMaxSubstitutions].
void validate(const CoachingConfig& config);

// The delay in ticks at this rate, of the delay in whole milliseconds, rounded
// up: an instruction never arrives early.
[[nodiscard]] std::int64_t instructionDelayTicks(const CoachingConfig& config, int ticksPerSecond);

inline constexpr std::string_view kCoachingSystemName = "coaching";

struct CoachingRules {
  CoachingConfig coaching{};
  int ticksPerSecond = kDefaultTicksPerSecond;
};

// Every tick, last of the standard systems (docs/match-engine.md#coaching):
// records InstructionGiven in the step a tactic was given, and once
// instructionDelayTicks() have passed since, gives the side that tactic and
// records TacticChanged. While the ball is dead for a set piece, makes the
// substitutions ordered so far in order -- SubstitutionMade --, or refuses one
// the limit, a player no longer on the pitch or one already used rules out --
// SubstitutionRefused. Throws std::invalid_argument for an invalid
// configuration.
[[nodiscard]] MatchSystem makeCoachingSystem(const CoachingRules& rules = {});

}  // namespace ElyverseFootball::SimMatch
