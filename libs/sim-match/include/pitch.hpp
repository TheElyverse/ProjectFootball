#pragma once

#include "vec2.hpp"

namespace ElyverseFootball::SimMatch {

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

 private:
  double lengthMeters_;
  double widthMeters_;
};

}  // namespace ElyverseFootball::SimMatch
