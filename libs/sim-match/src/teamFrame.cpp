#include "teamFrame.hpp"

namespace ElyverseFootball::SimMatch {

double depthOf(const TeamSide side, const SimCore::Vec2 position, const Pitch& pitch) noexcept {
  return side == TeamSide::kHome ? position.x : pitch.lengthMeters() - position.x;
}

double xAtDepth(const TeamSide side, const double depth, const Pitch& pitch) noexcept {
  return side == TeamSide::kHome ? depth : pitch.lengthMeters() - depth;
}

}  // namespace ElyverseFootball::SimMatch
