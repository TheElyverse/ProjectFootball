#pragma once

#include <cstddef>
#include <optional>

#include "ballPhysics.hpp"
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
  // The highest a ball can be and still be taken at the feet. A ball above
  // this passes over the player: heading it and challenging for it in the air
  // are their own systems, and until they exist a high ball simply runs
  // through.
  double controlHeight = 1.0;  // m
  // A goalkeeper in his own penalty area takes a ball there with his hands:
  // this far from him and this high (docs/goalkeeper.md).
  double handsRadius = 1.2;  // m
  double handsHeight = 2.2;  // m

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

// `ball` is the free ball at the start of the tick, `moved` the step
// stepFreeBallTimed() makes of it. Every player of `state` competes, moving
// as the movement system moves him in this tick, except the ball's last touch
// within
// reclaimDelaySeconds of it. The earliest contact wins; equal contact times
// go to the player who comes closer, and then to the lower id. A contact above
// controlHeight does not count: the ball flies over the player. A goalkeeper
// who stands in his own penalty area, with the ball in it, at the start of
// the tick reaches it with his hands instead: within handsRadius and up to
// handsHeight. The player at `excluded`, if any, does not compete: the
// goalkeeper facing a shot meets it his own way, and one busy with a dive
// (isDiving()) takes no ball at all (docs/shot-stopping.md).
// Empty if no one reaches the ball.
//
// The height at the moment of contact is asked of the flight itself
// (ballHeightAfter()), which is why `physics` is needed: neither end of the
// tick describes a ball that bounces on the way, and a ball that leaves the
// pitch ends the tick lying flat on the line however high it crossed it.
[[nodiscard]] std::optional<BallClaim> findBallClaim(const MatchState& state, const BallState& ball,
                                                     const BallStep& moved,
                                                     const BallPhysics& physics,
                                                     SimCore::SimTick now, double secondsPerTick,
                                                     const ReceptionConfig& config,
                                                     std::optional<std::size_t> excluded = {});

// How far from him and how high a player gets to a ball.
struct BallReach {
  double radius = 0.0;  // m
  double height = 0.0;  // m
};

// findBallClaim() for a ball outfield players do not take at their feet: a
// goalkeeper still reaches it as findBallClaim() says, everyone else as
// `outfield` says -- a body in the way of a shot rather than a foot on the
// ball (docs/shooting.md). What the first player to reach it does with it is
// the caller's to decide.
[[nodiscard]] std::optional<BallClaim> findBallContact(const MatchState& state,
                                                       const BallState& ball, const BallStep& moved,
                                                       const BallPhysics& physics,
                                                       SimCore::SimTick now, double secondsPerTick,
                                                       const ReceptionConfig& config,
                                                       const BallReach& outfield,
                                                       std::optional<std::size_t> excluded = {});

// Throws std::invalid_argument unless every value is finite and not
// negative.
void validate(const ReceptionConfig& config);

}  // namespace ElyverseFootball::SimMatch
