#include "playerMovement.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <optional>
#include <string>

#include "passCandidates.hpp"
#include "shotStopping.hpp"

namespace ElyverseFootball::SimMatch {
namespace {

using SimCore::Vec2;

// The velocity the player would like to have: toward the target, no faster
// than maxSpeed, and no faster than he can still stop from on the target,
// braking at his full acceleration from the next tick on. Zero without a
// target.
//
// Moving at v for one tick and then slowing by a·Δt per tick covers
// v²/(2a) + v·Δt/2; solving that for the distance d gives the braking speed
// v = (-a·Δt + sqrt((a·Δt)² + 8·a·d)) / 2. The continuous v = sqrt(2·a·d)
// ignores that a tick moves at the new speed for its whole length, so a
// player following it would arrive too fast and stop abruptly.
[[nodiscard]] Vec2 desiredVelocity(const PlayerMatchState& player,
                                   const double secondsPerTick) noexcept {
  if (!player.target) {
    return {};
  }
  const Vec2 offset = *player.target - player.position;
  const double distance = offset.length();
  if (distance == 0.0) {
    return {};
  }
  const double acceleration = player.attributes.acceleration;
  const double tickChange = acceleration * secondsPerTick;
  const double brakingSpeed =
      (-tickChange + std::sqrt((tickChange * tickChange) + (8.0 * acceleration * distance))) / 2.0;
  const double speed = std::min(player.attributes.maxSpeed, brakingSpeed);
  return offset * (speed / distance);
}

}  // namespace

PlayerKinematics stepPlayerMovement(const PlayerMatchState& player,
                                    const double secondsPerTick) noexcept {
  // Change the velocity toward the desired one by at most a·Δt. Both lie
  // within maxSpeed, so every point between them does too.
  const double maxChange = player.attributes.acceleration * secondsPerTick;
  Vec2 change = desiredVelocity(player, secondsPerTick) - player.velocity;
  const double changeLength = change.length();
  if (changeLength > maxChange) {
    change = change * (maxChange / changeLength);
  }
  const Vec2 velocity = player.velocity + change;
  const Vec2 displacement = velocity * secondsPerTick;

  // Arrival: a step toward the target that would reach or pass it ends on
  // it, at rest, if the player is slow enough to stop there -- at most two
  // ticks' worth of acceleration, which braking guarantees on a straight
  // approach. A player carried past a new target by his momentum, or still
  // moving away from one just behind him, runs on and comes back instead of
  // stopping dead or jumping back onto it.
  const double maxStopSpeed = 2.0 * maxChange;
  if (player.target) {
    const Vec2 offset = *player.target - player.position;
    const bool towardTarget = displacement.dot(offset) > 0.0;
    if (towardTarget && velocity.lengthSquared() <= maxStopSpeed * maxStopSpeed &&
        displacement.lengthSquared() >= offset.lengthSquared()) {
      return {.position = *player.target, .velocity = {}};
    }
  }
  return {.position = player.position + displacement, .velocity = velocity};
}

Vec2 facingAfterMove(const PlayerMatchState& player, const PlayerKinematics& moved,
                     const Vec2 ballPosition) noexcept {
  const double speed = moved.velocity.length();
  if (speed > kFacingRunSpeed) {
    return moved.velocity * (1.0 / speed);
  }
  const Vec2 towardBall = ballPosition - moved.position;
  const double distance = towardBall.length();
  if (distance > 0.0) {
    return towardBall * (1.0 / distance);
  }
  return player.facing;
}

Vec2 facingAfterMove(const PlayerMatchState& player, const PlayerKinematics& moved,
                     const BallState& ball) noexcept {
  if (ball.held && ball.owner == player.playerId && moved.velocity.length() <= kFacingRunSpeed) {
    return {.x = attackingDirection(player.side), .y = 0.0};
  }
  return facingAfterMove(player, moved, ball.position);
}

namespace {

// A keeper busy with a dive at the start of the step is where it takes him by
// its end, whatever his target: running, flying or down. Empty for anyone
// else.
[[nodiscard]] std::optional<PlayerKinematics> diveMove(const PlayerMatchState& player,
                                                       const KeeperDive& dive,
                                                       const MatchStepContext& context,
                                                       const ShotStoppingConfig& saves) {
  const double secondsPerTick = context.secondsPerTick();
  const double since =
      static_cast<double>(context.tick().value() - dive.tick.value()) * secondsPerTick;
  if (!isBusy(dive, since)) {
    return std::nullopt;
  }
  const Vec2 position =
      bodyAt(dive, std::min(since + secondsPerTick, dive.landSeconds), player.attributes, saves);
  Vec2 velocity = (position - player.position) * (1.0 / secondsPerTick);
  // A dive is quicker than his run; what the state keeps as his velocity is
  // what his legs allow.
  if (const double speed = velocity.length(); speed > player.attributes.maxSpeed) {
    velocity = velocity * (player.attributes.maxSpeed / speed);
  }
  return PlayerKinematics{.position = position, .velocity = velocity};
}

}  // namespace

MatchSystem makePlayerMovementSystem(const ShotStoppingConfig& saves) {
  validate(saves);
  return {.name = std::string(kPlayerMovementSystemName),
          .update = [saves](const MatchStepContext& context, const MatchState& current,
                            MatchStateWriter& next) {
            for (std::size_t index = 0; const PlayerMatchState& player : current.players()) {
              // A dive decided this very step counts.
              if (const auto& dive = next.tactical(index).dive) {
                if (const auto dived = diveMove(player, *dive, context, saves)) {
                  next.setPlayerPosition(index, dived->position);
                  next.setPlayerVelocity(index, dived->velocity);
                  ++index;
                  continue;
                }
              }
              const PlayerKinematics moved = stepPlayerMovement(player, context.secondsPerTick());
              next.setPlayerPosition(index, moved.position);
              next.setPlayerVelocity(index, moved.velocity);
              next.setPlayerFacing(index, facingAfterMove(player, moved, current.ball()));
              ++index;
            }
          }};
}

MatchSystem makePlayerMovementSystem() {
  return makePlayerMovementSystem(ShotStoppingConfig{});
}

}  // namespace ElyverseFootball::SimMatch
