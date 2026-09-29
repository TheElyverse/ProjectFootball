#include "ballMovement.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <string>

#include "passing.hpp"
#include "playerMovement.hpp"
#include "reception.hpp"
#include "restart.hpp"

namespace ElyverseFootball::SimMatch {
namespace {

using SimCore::PlayerId;
using SimCore::Vec2;

// The owner as the movement system leaves him after this tick: moved and
// turned with the same pure functions, so ball and carrier stay together.
[[nodiscard]] PlayerMatchState ownerAfterMove(const PlayerMatchState& owner,
                                              const Vec2 ballPosition,
                                              const double secondsPerTick) noexcept {
  const PlayerKinematics moved = stepPlayerMovement(owner, secondsPerTick);
  PlayerMatchState after = owner;
  after.facing = facingAfterMove(owner, moved, ballPosition);
  after.position = moved.position;
  after.velocity = moved.velocity;
  return after;
}

// The ball as a pass leaves it: free, at the passer's feet, with the executed
// pass velocity, and the passer as its last touch. At his feet is where a
// controlled ball already is -- except when he got it by a command in this
// very tick, which moves no ball. Every pass is a ground pass, so the ball
// stays on the grass without vertical velocity or spin; lofted passes and
// shots arrive with their own decisions.
[[nodiscard]] BallState kicked(const MatchState& state, const PassIntent& intent,
                               const BallPhysics& physics, const PassConfig& passing,
                               const MatchStepContext& context) {
  BallState ball = state.ball();
  // The passer owns the ball, so he is in the state.
  const auto passer = findPlayerIndex(state, intent.passer).value_or(0);
  ball.position = carriedBallPosition(state.players()[passer], physics, state.pitch());
  ball.velocity = executePass(intent, ball, state.players()[passer], passing,
                              context.random(SimCore::RandomNumberGeneratorDomain::kExecution),
                              passPressure(state, passer, passing));
  ball.owner = std::nullopt;
  ball.lastTouch = BallTouch{.playerId = intent.passer, .tick = context.tick()};
  ball.height = 0.0;
  ball.verticalVelocity = 0.0;
  ball.spin = 0.0;
  return ball;
}

// What gaining control of a free ball was: the ball's last touch kicked it,
// so a teammate of his received the pass and an opponent intercepted it. A
// ball nobody played, or one the kicker takes back himself, was loose.
[[nodiscard]] MatchEvent controlEvent(const MatchState& state, const BallState& ball,
                                      const BallClaim& claim, const Vec2 contactPosition,
                                      const SimCore::SimTick tick) {
  const PlayerMatchState& claimant = state.players()[claim.playerIndex];
  if (!ball.lastTouch || ball.lastTouch->playerId == claimant.playerId) {
    return LooseBallRecovered{
        .tick = tick, .player = claimant.playerId, .position = contactPosition};
  }
  const PlayerId passer = ball.lastTouch->playerId;
  const auto passerIndex = findPlayerIndex(state, passer);
  const bool teammate = passerIndex && state.players()[*passerIndex].side == claimant.side;
  if (teammate) {
    return PassReceived{.tick = tick, .receiver = claimant.playerId, .passer = passer};
  }
  return PassIntercepted{.tick = tick,
                         .interceptor = claimant.playerId,
                         .passer = passer,
                         .position = contactPosition};
}

}  // namespace

Vec2 carriedBallPosition(const PlayerMatchState& carrier, const BallPhysics& physics,
                         const Pitch& pitch) noexcept {
  return pitch.clamp(carrier.position + (carrier.facing * physics.carryDistance));
}

MatchSystem makeBallMovementSystem(const BallPhysics& physics, const PassConfig& passing,
                                   const ReceptionConfig& reception,
                                   const RestartConfig& restarts) {
  validate(physics);
  validate(passing);
  validate(reception);
  return {
      .name = std::string(kBallMovementSystemName),
      .update = [physics, passing, reception, restarts](const MatchStepContext& context,
                                                        const MatchState& current,
                                                        MatchStateWriter& next) {
        const double secondsPerTick = context.secondsPerTick();
        // Leaves the ball with this player, at his feet as he ends the tick:
        // on the ground, since that is where a player keeps a ball he has.
        const auto carryBy = [&](const PlayerMatchState& player) {
          const PlayerMatchState owner =
              ownerAfterMove(player, current.ball().position, secondsPerTick);
          next.setBallPosition(carriedBallPosition(owner, physics, current.pitch()));
          next.setBallVelocity(owner.velocity);
          next.setBallHeight(0.0);
          next.setBallVerticalVelocity(0.0);
          next.setBallSpin(0.0);
        };

        // 1. Play the pending pass, if its passer owns the ball. The pass
        //    itself comes from current, not next: a pass decided this same
        //    step is played next step, one step of pending pass being
        //    deliberate. The ball comes from next, and next.pendingPass() is
        //    checked too, because a challenge earlier this same step may
        //    have already taken the ball and dropped the pass, and current
        //    would still show the stale pre-step values.
        BallState ball = next.ball();
        if (const std::optional<PassIntent> intent = current.pendingPass();
            intent && next.pendingPass()) {
          next.setPendingPass(std::nullopt);
          if (ball.owner == intent->passer) {
            ball = kicked(current, *intent, physics, passing, context);
            next.setBallOwner(ball.owner);
            next.setBallLastTouch(ball.lastTouch);
            next.setLastPass(PassRecord{.passer = intent->passer,
                                        .from = ball.position,
                                        .tick = context.tick(),
                                        .receiver = intent->receiver});
            context.record(PassAttempted{.tick = context.tick(),
                                         .passer = intent->passer,
                                         .intendedReceiver = intent->receiver,
                                         .from = ball.position,
                                         .target = intent->target,
                                         .speed = ball.velocity.length()});
            context.record(PossessionChanged{
                .tick = context.tick(), .previousOwner = intent->passer, .newOwner = std::nullopt});
          }
        }

        // 2. A controlled ball stays with its owner. create() and the
        //    writer accept no owner outside the state.
        if (ball.owner) {
          if (const auto index = findPlayerIndex(current, *ball.owner)) {
            carryBy(current.players()[*index]);
          }
          return;
        }

        // 3. A free ball rolls, flies and bounces, and the first player to
        //    reach it on its way takes it -- unless it is already out of play
        //    and the
        //    restart system, which runs after this one, is there to settle
        //    who plays on: a player standing on the line must not receive or
        //    intercept the ball first and have the restart overwrite him.
        const BallStep step = stepFreeBallTimed(ball, physics, current.pitch(), secondsPerTick);
        const BallState& moved = step.ball;
        const bool awaitsRestart = restarts.enabled && isOutOfPlay(ball, current.pitch());
        if (awaitsRestart) {
          return;
        }
        if (const auto claim = findBallClaim(current, ball, step, physics, context.tick(),
                                             secondsPerTick, reception)) {
          next.setBallOwner(claim->playerId);
          next.setBallLastTouch(BallTouch{.playerId = claim->playerId, .tick = context.tick()});
          next.setLastReception(ReceptionRecord{.player = claim->playerId,
                                                .tick = context.tick(),
                                                .ballSpeed = ball.velocity.length()});
          carryBy(current.players()[claim->playerIndex]);
          const Vec2 contactPosition =
              ball.position + ((moved.position - ball.position) * claim->contact.contactFraction);
          context.record(controlEvent(current, ball, *claim, contactPosition, context.tick()));
          context.record(PossessionChanged{
              .tick = context.tick(), .previousOwner = std::nullopt, .newOwner = claim->playerId});
          return;
        }
        next.setBallPosition(moved.position);
        next.setBallVelocity(moved.velocity);
        next.setBallHeight(moved.height);
        next.setBallVerticalVelocity(moved.verticalVelocity);
        next.setBallSpin(moved.spin);
      }};
}

MatchSystem makeBallMovementSystem(const BallPhysics& physics, const PassConfig& passing,
                                   const ReceptionConfig& reception) {
  return makeBallMovementSystem(physics, passing, reception, RestartConfig{});
}

MatchSystem makeBallMovementSystem(const BallPhysics& physics, const PassConfig& passing) {
  return makeBallMovementSystem(physics, passing, ReceptionConfig{}, RestartConfig{});
}

MatchSystem makeBallMovementSystem(const BallPhysics& physics) {
  return makeBallMovementSystem(physics, PassConfig{}, ReceptionConfig{}, RestartConfig{});
}

}  // namespace ElyverseFootball::SimMatch
