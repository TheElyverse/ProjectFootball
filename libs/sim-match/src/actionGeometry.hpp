#pragma once

// Helpers shared by the off-ball and defensive action decisions; internal to
// sim-match.

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <string>

#include "desiredRegion.hpp"
#include "matchState.hpp"
#include "perception.hpp"
#include "tactic.hpp"
#include "vec2.hpp"

namespace ElyverseFootball::SimMatch::ActionGeometry {

// Eight directions around a point, starting along +x, counterclockwise.
inline constexpr double kDiagonal = 0.70710678118654752440;
inline constexpr std::array<SimCore::Vec2, 8> kDirections{{{.x = 1.0, .y = 0.0},
                                                           {.x = kDiagonal, .y = kDiagonal},
                                                           {.x = 0.0, .y = 1.0},
                                                           {.x = -kDiagonal, .y = kDiagonal},
                                                           {.x = -1.0, .y = 0.0},
                                                           {.x = -kDiagonal, .y = -kDiagonal},
                                                           {.x = 0.0, .y = -1.0},
                                                           {.x = kDiagonal, .y = -kDiagonal}}};

// The largest positioning penalty the region score counts: beyond it a
// target is simply far off shape.
inline constexpr double kMaxRegionPenalty = 2.0;

[[nodiscard]] inline double distanceBetween(const SimCore::Vec2 first,
                                            const SimCore::Vec2 second) noexcept {
  return std::sqrt((first - second).lengthSquared());
}

// The distance from a point to the segment from start to end.
[[nodiscard]] inline double distanceToSegment(const SimCore::Vec2 point, const SimCore::Vec2 start,
                                              const SimCore::Vec2 end) noexcept {
  const SimCore::Vec2 segment = end - start;
  const double lengthSquared = segment.lengthSquared();
  if (lengthSquared == 0.0) {
    return distanceBetween(point, start);
  }
  const double along = std::clamp((point - start).dot(segment) / lengthSquared, 0.0, 1.0);
  return distanceBetween(point, start + (segment * along));
}

// A unit vector from `from` toward `toward`; zero if they coincide.
[[nodiscard]] inline SimCore::Vec2 directionTo(const SimCore::Vec2 from,
                                               const SimCore::Vec2 toward) noexcept {
  const double length = distanceBetween(from, toward);
  return length > 0.0 ? (toward - from) * (1.0 / length) : SimCore::Vec2{};
}

// The tactic of a side; throws std::invalid_argument for a scripted side.
[[nodiscard]] inline const SimTactics::Tactic& tacticOf(const MatchState& state,
                                                        const TeamSide side) {
  const auto& tactic = state.tactics().of(side);
  if (!tactic) {
    throw std::invalid_argument(std::string("tactical decisions: ") +
                                std::string(teamSideName(side)) + " plays no tactic");
  }
  return *tactic;
}

// Everything that scores a candidate position against a player's desired
// region.
struct RegionContext {
  SimCore::SimTick now;
  double secondsPerTick = 0.0;
  const PositioningConfig* positioning = nullptr;
  const PerceptionConfig* perception = nullptr;
};

// How much a target costs more than the player's desired region, by the
// positioning cost, in [0, kMaxRegionPenalty].
[[nodiscard]] inline double regionPenalty(const MatchState& state, const std::size_t playerIndex,
                                          const SimCore::Vec2 target, const DesiredRegion& region,
                                          const RegionContext& context) {
  const double cost =
      evaluatePosition(state, playerIndex, target, region.tacticalTarget, context.now,
                       context.secondsPerTick, *context.positioning, *context.perception)
          .total;
  return std::clamp(cost - region.cost.total, 0.0, kMaxRegionPenalty);
}

}  // namespace ElyverseFootball::SimMatch::ActionGeometry
