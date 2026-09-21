#include "pitch.hpp"

#include <cmath>
#include <stdexcept>

namespace ElyverseFootball::SimMatch {

Pitch::Pitch(const double lengthMeters, const double widthMeters)
    : lengthMeters_(lengthMeters), widthMeters_(widthMeters) {
  if (!std::isfinite(lengthMeters) || lengthMeters <= 0.0 || !std::isfinite(widthMeters) ||
      widthMeters <= 0.0) {
    throw std::invalid_argument("Pitch dimensions must be positive, finite meters");
  }
}

bool Pitch::contains(const SimCore::Vec2 position) const noexcept {
  return position.isFinite() && position.x >= 0.0 && position.x <= lengthMeters_ &&
         position.y >= 0.0 && position.y <= widthMeters_;
}

}  // namespace ElyverseFootball::SimMatch
