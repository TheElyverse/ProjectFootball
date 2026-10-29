#pragma once

#include <cstdint>
#include <string_view>

namespace ElyverseFootball::SimMatch {

// Where in the box a cross is aimed (docs/on-ball.md#crossing): the near
// post, the penalty spot, the far post -- in the air -- or, along the
// ground, back to the edge of the box; or at a teammate's run.
enum class CrossArea : std::uint8_t {
  kNearPost,
  kPenaltySpot,
  kFarPost,
  kCutBack,
  kRun,
};

// "nearPost", "penaltySpot", "farPost", "cutBack", "run"; "unknown" outside
// the enumerators.
[[nodiscard]] std::string_view crossAreaName(CrossArea area) noexcept;

}  // namespace ElyverseFootball::SimMatch
