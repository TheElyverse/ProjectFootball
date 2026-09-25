#pragma once

#include <string_view>

#include "matchSimulation.hpp"
#include "matchState.hpp"
#include "vec2.hpp"

namespace ElyverseFootball::SimMatch {

// Where a player is and how fast he moves after one tick of movement.
struct PlayerKinematics {
  SimCore::Vec2 position;
  SimCore::Vec2 velocity;

  friend bool operator==(const PlayerKinematics&, const PlayerKinematics&) = default;
};

// One tick of the movement model described in docs/player-movement.md: the
// player steers toward his target at up to maxSpeed, changes his velocity by
// at most acceleration * secondsPerTick, and brakes in time to stop on the
// target instead of running past it. Without a target he slows to a stop.
//
// A pure function of the player and the tick length, so other systems -- the
// ball following its carrier, arrival-time estimates -- can predict movement
// with exactly the same rule.
[[nodiscard]] PlayerKinematics stepPlayerMovement(const PlayerMatchState& player,
                                                  double secondsPerTick) noexcept;

// Speed above which a player looks where he runs.
inline constexpr double kFacingRunSpeed = 1.0;  // m/s

// Where a player looks after a tick of movement took him to `moved`: along
// his run while he moves faster than kFacingRunSpeed, otherwise toward the
// ball, and where he looked before if the ball lies exactly on his spot. He
// turns at once; a turning rate comes with body orientation. Always a unit
// vector.
[[nodiscard]] SimCore::Vec2 facingAfterMove(const PlayerMatchState& player,
                                            const PlayerKinematics& moved,
                                            SimCore::Vec2 ballPosition) noexcept;

inline constexpr std::string_view kPlayerMovementSystemName = "player movement";

// Moves every player one tick with stepPlayerMovement() and turns him with
// facingAfterMove(). Writes player positions, velocities and facings, every
// tick.
[[nodiscard]] MatchSystem makePlayerMovementSystem();

}  // namespace ElyverseFootball::SimMatch
