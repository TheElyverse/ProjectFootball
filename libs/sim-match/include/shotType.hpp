#pragma once

#include <cstdint>
#include <string_view>

namespace ElyverseFootball::SimMatch {

// How a shot is struck (docs/on-ball.md#shot-types): driven hard and straight
// with topspin, curled around what is in the way with sidespin, or chipped
// high over the keeper with backspin.
enum class ShotType : std::uint8_t {
  kDriven,
  kCurled,
  kChipped,
};

// "driven", "curled", "chipped"; "unknown" outside the enumerators.
[[nodiscard]] std::string_view shotTypeName(ShotType type) noexcept;

}  // namespace ElyverseFootball::SimMatch
