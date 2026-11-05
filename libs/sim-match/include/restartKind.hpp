#pragma once

#include <cstdint>
#include <string_view>

namespace ElyverseFootball::SimMatch {

// How play restarts after the ball went out or into a goal
// (docs/match-engine.md#restarts).
enum class RestartKind : std::uint8_t {
  kThrowIn,
  kGoalKick,
  kCorner,
  kKickoff,
  // An indirect free kick, after an offside (docs/match-engine.md#offside).
  kIndirectFreeKick,
  // A direct free kick and a penalty, after a foul (docs/fouls.md).
  kDirectFreeKick,
  kPenalty,
};

// "throwIn", "goalKick", "corner", "kickoff", "indirectFreeKick",
// "directFreeKick", "penalty"; "unknown" outside the enumerators.
[[nodiscard]] std::string_view restartKindName(RestartKind kind) noexcept;

// Simplified restarts (docs/match-engine.md#restarts): opt-in, so matches
// without them keep the ball stopping on the line as before. Part of
// MatchConfig and of every replay. Declared here, next to the kinds, so ball
// movement can also take it without depending on the restart system.
struct RestartConfig {
  bool enabled = false;
  // A goalkeeper who holds the ball in his hands longer than this concedes
  // a corner (Law 12).
  double maxHoldSeconds = 8.0;  // s

  friend bool operator==(const RestartConfig&, const RestartConfig&) = default;
};

}  // namespace ElyverseFootball::SimMatch
