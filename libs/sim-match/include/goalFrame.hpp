#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

#include "ballPhysics.hpp"
#include "matchEvents.hpp"
#include "matchState.hpp"
#include "pitch.hpp"

namespace ElyverseFootball::SimMatch {

// The goal frame as something a ball hits; see docs/on-ball.md#shooting. Part
// of MatchConfig and therefore of every replay.
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

// When a free ball reaches a vertical plane if nothing stops it, and the ball
// at that moment: the plane through `origin` square to the unit `normal`,
// which points the way the ball crosses it. It follows ballAfter() -- curve
// included -- which knows no pitch boundary. Empty for a ball that stops
// short; the ball itself for one already on or beyond the plane.
struct BallPassage {
  double seconds = 0.0;
  BallState ball;
};

[[nodiscard]] std::optional<BallPassage> predictPlanePassage(const BallState& ball,
                                                             const BallPhysics& physics,
                                                             SimCore::Vec2 origin,
                                                             SimCore::Vec2 normal) noexcept;

// Where a free ball crosses a goal line if nothing stops it: the pitch y and
// the height it crosses at.
struct GoalLineCrossing {
  double y = 0.0;
  double height = 0.0;
};

// Empty for a ball that stops short of that goal line, and for one without
// sidespin that is not moving toward it.
[[nodiscard]] std::optional<GoalLineCrossing> predictGoalLineCrossing(const BallState& ball,
                                                                      const BallPhysics& physics,
                                                                      const Pitch& pitch,
                                                                      GoalEnd end) noexcept;

}  // namespace ElyverseFootball::SimMatch
