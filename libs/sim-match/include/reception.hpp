#pragma once

#include <cstddef>
#include <optional>

#include "ids.hpp"
#include "matchState.hpp"
#include "simTime.hpp"
#include "vec2.hpp"

namespace ElyverseFootball::SimMatch {

// How players gain control of a free ball; see docs/reception.md. Part of
// MatchConfig and therefore of every replay.
struct ReceptionConfig {
  // A player controls a free ball that comes this close to him.
  double controlRadius = 1.0;  // m
  // The last player to touch the ball cannot take it back for this long, so
  // a pass does not stick to the passer's foot.
  double reclaimDelaySeconds = 0.3;

  friend bool operator==(const ReceptionConfig&, const ReceptionConfig&) = default;
};

// When, during a tick, a moving player and a moving ball first come within
// radius of each other. Both move in a straight line over the tick, from
// their start to their end position. contactFraction is that moment as a
// fraction of the tick, 0 if they start within reach; closestDistance is how
// close they come at all during the tick.
struct Contact {
  double contactFraction = 0.0;
  double closestDistance = 0.0;
};

// Empty if the two never come within radius during the tick.
[[nodiscard]] std::optional<Contact> findContact(SimCore::Vec2 playerFrom, SimCore::Vec2 playerTo,
                                                 SimCore::Vec2 ballFrom, SimCore::Vec2 ballTo,
                                                 double radius) noexcept;

// The player who gains control of a free ball moving from ballFrom to ballTo
// in the step of tick now, if any.
struct BallClaim {
  // Index in MatchState::players().
  std::size_t playerIndex = 0;
  SimCore::PlayerId playerId;
  Contact contact;
};

// `ball` is the free ball at the start of the tick, ballTo where it rolls to
// by the end. Every player of `state` competes, moving as the movement system
// moves him in this tick, except the ball's last touch within
// reclaimDelaySeconds of it. The earliest contact wins; equal contact times
// go to the player who comes closer, and then to the lower id. Empty if no
// one reaches the ball.
[[nodiscard]] std::optional<BallClaim> findBallClaim(const MatchState& state, const BallState& ball,
                                                     SimCore::Vec2 ballTo, SimCore::SimTick now,
                                                     double secondsPerTick,
                                                     const ReceptionConfig& config);

// Throws std::invalid_argument unless both values are finite and not
// negative.
void validate(const ReceptionConfig& config);

}  // namespace ElyverseFootball::SimMatch
