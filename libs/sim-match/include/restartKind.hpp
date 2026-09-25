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

}  // namespace ElyverseFootball::SimMatch
