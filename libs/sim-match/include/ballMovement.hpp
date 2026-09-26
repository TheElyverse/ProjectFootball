#pragma once

#include <string_view>

#include "matchSimulation.hpp"
#include "matchState.hpp"
#include "pitch.hpp"
#include "restartKind.hpp"
#include "vec2.hpp"

namespace ElyverseFootball::SimMatch {

// Rolling resistance of a ball on grass used when a caller does not state
// one. A ground pass leaving the foot at 10 m/s rolls 10² / (2 · 1.5) ≈ 33 m.
inline constexpr double kDefaultRollingDeceleration = 1.5;  // m/s²

// How far ahead of his feet a player carries a controlled ball.
inline constexpr double kDefaultCarryDistance = 0.5;  // m

// The physical constants of the ball model. The rolling deceleration must be
// positive and finite, the carry distance finite and not negative.
struct BallPhysics {
  double rollingDeceleration = kDefaultRollingDeceleration;
  double carryDistance = kDefaultCarryDistance;

  friend bool operator==(const BallPhysics&, const BallPhysics&) = default;
};

// One tick of a free, rolling ball as described in docs/ball-movement.md:
// ground friction slows it at a constant rate along its direction of travel
// until it stops -- never reversing it -- and a ball that leaves the pitch
// stops on the line where it crossed it.
//
// The integration is exact for constant deceleration, so a ball rolling from
// speed v stops after v² / (2 · rollingDeceleration) meters, independent of
// the tick rate.
[[nodiscard]] BallState stepFreeBall(const BallState& ball, const BallPhysics& physics,
                                     const Pitch& pitch, double secondsPerTick) noexcept;

// Where a controlled ball is: carryDistance ahead of its carrier along his
// facing, moved onto the pitch if that lies off it -- a carrier on the line
// does not carry the ball out of play (docs/possession.md).
[[nodiscard]] SimCore::Vec2 carriedBallPosition(const PlayerMatchState& carrier,
                                                const BallPhysics& physics,
                                                const Pitch& pitch) noexcept;

// Distance a ball rolling at this speed covers before it stops, in meters.
[[nodiscard]] double rollingDistance(double speed, const BallPhysics& physics) noexcept;

inline constexpr std::string_view kBallMovementSystemName = "ball movement";

struct PassConfig;
struct ReceptionConfig;

// Moves the ball one tick, every tick:
//
//   1. A pending pass is played if its passer owns the ball: the ball is
//      released with executePass()'s velocity and the passer recorded as its
//      last touch and in the state's last pass. A pass whose passer does not
//      own the ball is discarded. Either way the pending pass is cleared.
//   2. A controlled ball follows its owner: it ends the tick at
//      carriedBallPosition() of the owner as the movement system moves and
//      turns him in the same tick, with his velocity.
//   3. A free ball rolls with stepFreeBall(), and findBallClaim() decides
//      whether a player reaches it on its way this tick. The claimant owns it
//      from the end of the tick, with the ball at his feet, and becomes its
//      last touch; the state's last reception records him and how fast the
//      ball came. With restarts enabled, a ball already out of play is left
//      alone: the restart system settles who plays on, so a player standing
//      on it does not receive or intercept it first.
//
// Writes the ball's position, velocity, owner and last touch, the last pass
// and reception, and clears the pending pass. Throws std::invalid_argument for an invalid
// configuration. The shorter overloads use the default configuration for what they omit.
//
// Pair it with makePlayerMovementSystem(), both every tick, as
// makeMatchSystems() does: a controlled ball follows the carrier's move as the
// movement system makes it, and without that system the ball would end the
// tick where the carrier would have gone.
[[nodiscard]] MatchSystem makeBallMovementSystem(const BallPhysics& physics,
                                                 const PassConfig& passing,
                                                 const ReceptionConfig& reception,
                                                 const RestartConfig& restarts);
[[nodiscard]] MatchSystem makeBallMovementSystem(const BallPhysics& physics,
                                                 const PassConfig& passing,
                                                 const ReceptionConfig& reception);
[[nodiscard]] MatchSystem makeBallMovementSystem(const BallPhysics& physics,
                                                 const PassConfig& passing);
[[nodiscard]] MatchSystem makeBallMovementSystem(const BallPhysics& physics);

}  // namespace ElyverseFootball::SimMatch
