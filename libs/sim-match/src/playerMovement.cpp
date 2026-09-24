#include "playerMovement.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>

namespace ElyverseFootball::SimMatch {
namespace {

using SimCore::Vec2;

// std::sqrt is correctly rounded by IEEE 754, unlike std::hypot, so lengths
// computed this way are the same on every conforming platform.
[[nodiscard]] double lengthOf(const Vec2 vector) noexcept {
  return std::sqrt(vector.lengthSquared());
}

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
  const double distance = lengthOf(offset);
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
  const double changeLength = lengthOf(change);
  if (changeLength > maxChange) {
    change = change * (maxChange / changeLength);
  }
  const Vec2 velocity = player.velocity + change;
  const Vec2 displacement = velocity * secondsPerTick;

  // Arrival: a step that would reach or pass the target ends on it, at rest,
  // if the player is slow enough to stop there -- at most two ticks' worth of
  // acceleration, which braking guarantees on a straight approach. A player
  // carried past a new target by his momentum runs on and comes back
  // instead of stopping dead.
  const double maxStopSpeed = 2.0 * maxChange;
  if (player.target && velocity.lengthSquared() <= maxStopSpeed * maxStopSpeed &&
      displacement.lengthSquared() >= (*player.target - player.position).lengthSquared()) {
    return {.position = *player.target, .velocity = {}};
  }
  return {.position = player.position + displacement, .velocity = velocity};
}

MatchSystem makePlayerMovementSystem() {
  return {.name = std::string(kPlayerMovementSystemName),
          .update = [](const MatchStepContext& context, const MatchState& current,
                       MatchStateWriter& next) {
            for (std::size_t index = 0; const PlayerMatchState& player : current.players()) {
              const PlayerKinematics moved = stepPlayerMovement(player, context.secondsPerTick());
              next.setPlayerPosition(index, moved.position);
              next.setPlayerVelocity(index, moved.velocity);
              ++index;
            }
          }};
}

}  // namespace ElyverseFootball::SimMatch
