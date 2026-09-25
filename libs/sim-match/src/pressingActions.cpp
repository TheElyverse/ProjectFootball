#include "pressingActions.hpp"

#include <algorithm>
#include <cmath>

#include "actionGeometry.hpp"

namespace ElyverseFootball::SimMatch {

using ActionGeometry::directionTo;
using ActionGeometry::distanceBetween;
using SimCore::Vec2;

Vec2 pressTarget(const Vec2 carrier, const std::optional<Vec2>& option, const Vec2 ownGoal,
                 const double pressDistance) noexcept {
  const Vec2 toward = option ? *option : ownGoal;
  return carrier + (directionTo(carrier, toward) * pressDistance);
}

Vec2 laneBlockTarget(const PassingLane& lane, const Vec2 blocker,
                     const double minDistance) noexcept {
  const Vec2 carrier = lane.carrier;
  const Vec2 receiver = lane.receiver;
  const double length = distanceBetween(carrier, receiver);
  const Vec2 direction = directionTo(carrier, receiver);
  if (length < 2.0 * minDistance) {
    return carrier + (direction * (length / 2.0));
  }
  const double along =
      std::clamp((blocker - carrier).dot(direction), minDistance, length - minDistance);
  return carrier + (direction * along);
}

Vec2 coverTarget(const Vec2 presser, const Vec2 ownGoal, const double distance) noexcept {
  return presser + (directionTo(presser, ownGoal) * distance);
}

}  // namespace ElyverseFootball::SimMatch
