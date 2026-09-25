#pragma once

#include <stdexcept>

#include "vec2.hpp"

namespace ElyverseFootball::SimMatch {

// An axis-aligned rectangle in pitch coordinates, in meters, edges included:
// a region such as a zone or part of a grid. It may reach past the pitch.
struct PitchRect {
  SimCore::Vec2 min;
  SimCore::Vec2 max;

  [[nodiscard]] bool contains(const SimCore::Vec2 point) const noexcept {
    return point.x >= min.x && point.x <= max.x && point.y >= min.y && point.y <= max.y;
  }

  friend bool operator==(const PitchRect&, const PitchRect&) = default;
};

// Metric rectangle: (0, 0) is a corner, +x runs along the length toward the
// opposite goal line, +y along the width toward the opposite touchline.
// Coordinates are fixed to the pitch, independent of either team's direction.
class Pitch {
 public:
  // Both dimensions must be positive and finite, otherwise throws
  // std::invalid_argument. No default size or competition rules are imposed.
  explicit Pitch(double lengthMeters, double widthMeters);

  [[nodiscard]] double lengthMeters() const noexcept { return lengthMeters_; }
  [[nodiscard]] double widthMeters() const noexcept { return widthMeters_; }

  // Includes all edges and corners. Non-finite positions are never inside.
  // Tests a point only; ball radius and out-of-play rules belong elsewhere.
  [[nodiscard]] bool contains(SimCore::Vec2 position) const noexcept;

  // The point on the pitch nearest to a finite position: the position itself
  // if contains() holds, otherwise its projection onto the nearest edge or
  // corner.
  [[nodiscard]] SimCore::Vec2 clamp(SimCore::Vec2 position) const noexcept;

  // Compares both dimensions exactly, like Vec2 does. Two pitches built from
  // the same numbers are the same pitch; nothing here applies a tolerance.
  friend bool operator==(const Pitch&, const Pitch&) = default;

 private:
  double lengthMeters_;
  double widthMeters_;
};

}  // namespace ElyverseFootball::SimMatch
