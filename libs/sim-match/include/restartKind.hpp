#pragma once

#include <cstdint>
#include <string_view>

namespace ElyverseFootball::SimMatch {

// How play restarts after the ball went out (docs/restarts.md).
enum class RestartKind : std::uint8_t {
  kThrowIn,
  kGoalKick,
  kCorner,
};

// "throwIn", "goalKick", "corner"; "unknown" outside the enumerators.
[[nodiscard]] std::string_view restartKindName(RestartKind kind) noexcept;

// Simplified restarts (docs/restarts.md): opt-in, so matches without them
// keep the ball stopping on the line as before. Part of MatchConfig and of
// every replay. Declared here, next to the kinds, so ball movement can also
// take it without depending on the restart system.
struct RestartConfig {
  bool enabled = false;

  friend bool operator==(const RestartConfig&, const RestartConfig&) = default;
};

}  // namespace ElyverseFootball::SimMatch
