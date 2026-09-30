#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

#include "ballPhysics.hpp"
#include "matchState.hpp"
#include "pitch.hpp"

namespace ElyverseFootball::SimMatch {

// The goal frame as something a ball hits; see docs/shooting.md. Part of
// MatchConfig and therefore of every replay.
struct WoodworkConfig {
  // Posts and crossbar are round, this thick from their axis. The posts
  // stand on the goal line at the pitch geometry's posts, and the crossbar
  // lies on top of the goal's height, its underside.
  double radius = 0.06;  // m
  // The share of its speed into the frame a ball leaves it with.
  double restitution = 0.5;

  friend bool operator==(const WoodworkConfig&, const WoodworkConfig&) = default;
};

// Throws std::invalid_argument unless the radius is positive and finite and
// the restitution lies in [0, 1].
void validate(const WoodworkConfig& config);

// A part of a goal frame: the post at the lower or the higher pitch y, or
// the crossbar.
enum class WoodworkPart : std::uint8_t {
  kPostAtMinY,
  kPostAtMaxY,
  kCrossbar,
};

// "postAtMinY", "postAtMaxY", "crossbar"; "unknown" outside the enumerators.
[[nodiscard]] std::string_view woodworkPartName(WoodworkPart part) noexcept;

// A free ball meeting a goal frame: which, when, and the ball at that moment
// as it arrives and as it leaves. The rebound mirrors the ball's speed into
// the frame, scaled by the restitution, and keeps the rest; the ball loses
// its spin.
struct WoodworkHit {
  GoalEnd end = GoalEnd::kMinX;
  WoodworkPart part = WoodworkPart::kCrossbar;
  double seconds = 0.0;
  BallState ball;
  BallState rebound;
};

// The first time within `seconds` that a free ball touches a post or a
// crossbar while moving into it, if it does. The ball is a sphere of
// kBallRadius whose lowest point is its height. It follows ballAfter(), which
// knows no pitch boundary: a hit beyond the line the ball leaves the pitch
// over never happens, and the caller discards it.
[[nodiscard]] std::optional<WoodworkHit> findWoodworkHit(const BallState& ball,
                                                         const BallPhysics& physics,
                                                         const Pitch& pitch,
                                                         const WoodworkConfig& config,
                                                         double seconds) noexcept;

// Where a free ball crosses a goal line if nothing stops it: the pitch y and
// the height it crosses at.
struct GoalLineCrossing {
  double y = 0.0;
  double height = 0.0;
};

// Empty for a ball that is not moving toward that goal line or stops short
// of it.
[[nodiscard]] std::optional<GoalLineCrossing> predictGoalLineCrossing(const BallState& ball,
                                                                      const BallPhysics& physics,
                                                                      const Pitch& pitch,
                                                                      GoalEnd end) noexcept;

}  // namespace ElyverseFootball::SimMatch
