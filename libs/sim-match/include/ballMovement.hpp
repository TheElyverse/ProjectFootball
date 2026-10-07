#pragma once

#include <string_view>

#include "ballPhysics.hpp"
#include "matchSimulation.hpp"
#include "matchState.hpp"
#include "pitch.hpp"
#include "restartKind.hpp"
#include "vec2.hpp"

namespace ElyverseFootball::SimMatch {

// Where a controlled ball is: carryDistance ahead of its carrier along his
// facing, moved onto the pitch if that lies off it -- a carrier on the line
// does not carry the ball out of play (docs/possession.md). A carried ball is
// always on the ground.
[[nodiscard]] SimCore::Vec2 carriedBallPosition(const PlayerMatchState& carrier,
                                                const BallPhysics& physics,
                                                const Pitch& pitch) noexcept;

inline constexpr std::string_view kBallMovementSystemName = "ball movement";

struct AerialConfig;
struct PassConfig;
struct ReceptionConfig;
struct ShotConfig;
struct ShotStoppingConfig;
struct WoodworkConfig;

// Moves the ball one tick, every tick:
//
//   1. A pending pass is played if its passer owns the ball: the ball is
//      released with executePass()'s velocity and the passer recorded as its
//      last touch and in the state's last pass. A pass is a ground pass, so it
//      leaves the foot on the grass, without vertical velocity or spin. A
//      pending shot is struck the same way, on executeShot()'s flight, and
//      recorded as the state's last shot (docs/shooting.md). An action whose
//      player does not own the ball is discarded. Either way the pending
//      action is cleared.
//   2. A controlled ball follows its owner: it ends the tick at
//      carriedBallPosition() of the owner as the movement system moves and
//      turns him in the same tick, with his velocity, on the ground.
//   3. A free ball rolls, flies and bounces with stepFreeBall(), and
//      findBallClaim() decides whether a player reaches it on its way this
//      tick. The claimant owns it from the end of the tick, with the ball at
//      his feet, and becomes its last touch; the state's last reception records
//      him and how fast the ball came. A ball flying over his head is out of
//      reach and is not claimed (docs/reception.md). With restarts enabled, a
//      ball already out of play is left alone: the restart system settles who
//      plays on, so a player standing on it does not receive or intercept it
//      first. On its way the ball rebounds from a post or a crossbar it hits
//      (findWoodworkHit()), and a shot still fast comes off an outfield
//      player in its way instead of being taken by him (deflectShot()). The
//      goalkeeper facing a shot (facingKeeper()) does not take it like any
//      ball: where it passes his plane he meets it with his dive, or standing
//      set before he has reacted, and holds it, parries it or lets it past.
//      A keeper standing set is down for a moment after it passed him, and
//      one who holds it is up at once (docs/shot-stopping.md). A ball higher
//      than controlHeight that a player gets to before anyone takes it at his
//      feet is contested in the air (findAerialContact()): everyone near it
//      goes up, and the winner heads it -- at goal, to a teammate or clear --
//      or, a keeper with his hands, catches or punches it; nobody may reach it
//      at all (docs/aerial-duels.md).
//   4. A ball that leaves the pitch over a goal line, between the posts and
//      under the crossbar, is a goal: the score goes up and GoalScored is
//      recorded. A shot gets its one ShotResolved when the ball goes in, a
//      player takes it, it leaves the pitch or comes to rest.
//
// Writes the ball's position, velocity, height, vertical velocity, spin, owner
// and last touch, the last pass, shot and reception, the score and the last
// goal, a goalkeeper's dive in his tactical state and the position and
// velocity of one who holds the ball from a dive, every player's last jump,
// and clears the pending actions.
// Throws std::invalid_argument for an invalid
// configuration. The shorter overloads use the default configuration for what they omit.
//
// Pair it with makePlayerMovementSystem(), both every tick, as
// makeMatchSystems() does: a controlled ball follows the carrier's move as the
// movement system makes it, and without that system the ball would end the
// tick where the carrier would have gone.
[[nodiscard]] MatchSystem makeBallMovementSystem(
    const BallPhysics& physics, const PassConfig& passing, const ReceptionConfig& reception,
    const RestartConfig& restarts, const ShotConfig& shooting, const WoodworkConfig& woodwork,
    const ShotStoppingConfig& saves, const AerialConfig& aerial);
[[nodiscard]] MatchSystem makeBallMovementSystem(
    const BallPhysics& physics, const PassConfig& passing, const ReceptionConfig& reception,
    const RestartConfig& restarts, const ShotConfig& shooting, const WoodworkConfig& woodwork,
    const ShotStoppingConfig& saves);
[[nodiscard]] MatchSystem makeBallMovementSystem(
    const BallPhysics& physics, const PassConfig& passing, const ReceptionConfig& reception,
    const RestartConfig& restarts, const ShotConfig& shooting, const WoodworkConfig& woodwork);
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
