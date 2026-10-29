#pragma once

#include <string_view>

#include "ballPhysics.hpp"
#include "matchSimulation.hpp"
#include "matchState.hpp"
#include "shotStopping.hpp"
#include "vec2.hpp"

namespace ElyverseFootball::SimMatch {

// Where a player is and how fast he moves after one tick of movement.
struct PlayerKinematics {
  SimCore::Vec2 position;
  SimCore::Vec2 velocity;

  friend bool operator==(const PlayerKinematics&, const PlayerKinematics&) = default;
};

// One tick of the movement model described in docs/players.md#player-movement:
// the player steers toward his target at up to maxSpeed, changes his velocity
// by at most acceleration * secondsPerTick, and brakes in time to stop on the
// target instead of running past it. Without a target he slows to a stop.
[[nodiscard]] PlayerKinematics stepPlayerMovement(const PlayerMatchState& player,
                                                  double secondsPerTick) noexcept;

// The fastest a player runs: the share of his maxSpeed his fitness leaves him
// (docs/players.md#fitness), and a share of that with the ball at
// his feet or rolling ahead of him -- minCarrySpeedFactor at no ball control,
// maxCarrySpeedFactor at full control, linear in between
// (docs/on-ball.md#ball-carrying). A goalkeeper holding the ball in his hands
// runs as fast as without it.
[[nodiscard]] double runningSpeed(const PlayerMatchState& player, const BallState& ball,
                                  const BallPhysics& physics) noexcept;

// The player with his runningSpeed() as his maxSpeed: as he runs, or plans to
// run, with the ball.
[[nodiscard]] PlayerMatchState atRunningSpeed(const PlayerMatchState& player, const BallState& ball,
                                              const BallPhysics& physics) noexcept;

// stepPlayerMovement() at the player's runningSpeed(): the one rule every
// system that moves a player, or predicts his move, uses.
[[nodiscard]] PlayerKinematics stepPlayerMovement(const PlayerMatchState& player,
                                                  const BallState& ball, const BallPhysics& physics,
                                                  double secondsPerTick) noexcept;

// The player as the movement system moves him at the start of tick now:
// without a target while he is off his feet after a duel (isStumbling()), so
// he slows to a stop wherever he was going.
[[nodiscard]] PlayerMatchState asMoving(const PlayerMatchState& player,
                                        const PlayerTacticalState& tactical,
                                        SimCore::SimTick now) noexcept;

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

// facingAfterMove() toward this ball, except that a goalkeeper who holds it
// in his hands and no longer runs looks up the pitch
// (docs/goalkeeper.md#the-ball-in-his-hands).
[[nodiscard]] SimCore::Vec2 facingAfterMove(const PlayerMatchState& player,
                                            const PlayerKinematics& moved,
                                            const BallState& ball) noexcept;

inline constexpr std::string_view kPlayerMovementSystemName = "player movement";

struct ShotStoppingConfig;

// What the movement system is configured with: the default configuration
// for whatever a caller leaves out.
struct PlayerMovementRules {
  ShotStoppingConfig saves{};
  BallPhysics physics{};
};

// Moves every player one tick with stepPlayerMovement() at his runningSpeed(),
// as asMoving() has him, and turns him with facingAfterMove() toward the ball
// as the systems before it left it -- except a goalkeeper busy with a dive,
// whom it puts where his dive takes him, under his hands (bodyAt(),
// docs/goalkeeper.md#shot-stopping), whatever his target, without turning him.
// Writes player positions, velocities and facings, every tick. Throws
// std::invalid_argument for an invalid configuration.
[[nodiscard]] MatchSystem makePlayerMovementSystem(const PlayerMovementRules& rules = {});

}  // namespace ElyverseFootball::SimMatch
