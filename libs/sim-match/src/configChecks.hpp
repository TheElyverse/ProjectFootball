#pragma once

// Range checks the validate() functions build on; internal to sim-match. Each
// is false for NaN.

#include <cmath>
#include <limits>

namespace ElyverseFootball::SimMatch::ConfigChecks {

[[nodiscard]] inline bool isFinite(const double value) noexcept {
  return std::abs(value) <= std::numeric_limits<double>::max();
}

[[nodiscard]] inline bool isFiniteNonNegative(const double value) noexcept {
  return value >= 0.0 && value <= std::numeric_limits<double>::max();
}

[[nodiscard]] inline bool isPositiveFinite(const double value) noexcept {
  return value > 0.0 && value <= std::numeric_limits<double>::max();
}

// In [0, 1].
[[nodiscard]] inline bool isShare(const double value) noexcept {
  return value >= 0.0 && value <= 1.0;
}

// In [min, max].
[[nodiscard]] inline bool isWithin(const double value, const double min,
                                   const double max) noexcept {
  return value >= min && value <= max;
}

}  // namespace ElyverseFootball::SimMatch::ConfigChecks
