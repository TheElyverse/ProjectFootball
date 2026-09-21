#pragma once

#include <cmath>

namespace ElyverseFootball::SimCore {

// A value type shared by spatial systems. Units are defined by the caller
// (e.g. meters for positions, meters per second for velocities).
// Arithmetic follows double's floating-point rules; validate external inputs
// with isFinite(). Equality is exact, not a geometric tolerance comparison.
struct Vec2 {
  double x = 0.0;
  double y = 0.0;

  [[nodiscard]] bool isFinite() const noexcept { return std::isfinite(x) && std::isfinite(y); }

  [[nodiscard]] double length() const noexcept { return std::hypot(x, y); }

  [[nodiscard]] constexpr double dot(const Vec2 other) const noexcept {
    return (x * other.x) + (y * other.y);
  }

  friend constexpr bool operator==(Vec2, Vec2) noexcept = default;

  friend constexpr Vec2 operator+(const Vec2 left, const Vec2 right) noexcept {
    return {.x = left.x + right.x, .y = left.y + right.y};
  }

  friend constexpr Vec2 operator-(const Vec2 left, const Vec2 right) noexcept {
    return {.x = left.x - right.x, .y = left.y - right.y};
  }

  friend constexpr Vec2 operator*(const Vec2 vector, const double scalar) noexcept {
    return {.x = vector.x * scalar, .y = vector.y * scalar};
  }

  friend constexpr Vec2 operator*(const double scalar, const Vec2 vector) noexcept {
    return vector * scalar;
  }
};

[[nodiscard]] inline double distance(const Vec2 start, const Vec2 end) noexcept {
  return (end - start).length();
}

}  // namespace ElyverseFootball::SimCore
