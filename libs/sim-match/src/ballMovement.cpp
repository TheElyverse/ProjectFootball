#include "ballMovement.hpp"

#include <algorithm>
#include <cmath>
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

// The fraction t in (0, 1] of the move from start to end at which the ball
// crosses the pitch boundary, for a start on the pitch and an end off it.
// Checked per boundary line; the earliest crossing is where the ball leaves.
[[nodiscard]] double exitFraction(const Vec2 start, const Vec2 end, const Pitch& pitch) noexcept {
  double fraction = 1.0;
  const auto crossing = [&fraction](const double from, const double until, const double line) {
    const bool crosses = (from <= line && until > line) || (from >= line && until < line);
    if (crosses) {
      fraction = std::min(fraction, (line - from) / (until - from));
    }
  };
  crossing(start.x, end.x, 0.0);
  crossing(start.x, end.x, pitch.lengthMeters());
  crossing(start.y, end.y, 0.0);
  crossing(start.y, end.y, pitch.widthMeters());
  return fraction;
}

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
// very tick, which moves no ball.
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

[[nodiscard]] bool isValid(const BallPhysics& physics) noexcept {
  constexpr double kMax = std::numeric_limits<double>::max();
  return physics.rollingDeceleration > 0.0 && physics.rollingDeceleration <= kMax &&
         physics.carryDistance >= 0.0 && physics.carryDistance <= kMax;
}

}  // namespace

Vec2 carriedBallPosition(const PlayerMatchState& carrier, const BallPhysics& physics,
                         const Pitch& pitch) noexcept {
  return pitch.clamp(carrier.position + (carrier.facing * physics.carryDistance));
}

double rollingDistance(const double speed, const BallPhysics& physics) noexcept {
  return (speed * speed) / (2.0 * physics.rollingDeceleration);
}

BallState stepFreeBall(const BallState& ball, const BallPhysics& physics, const Pitch& pitch,
                       const double secondsPerTick) noexcept {
  // std::sqrt rather than std::hypot: correctly rounded on every platform.
  const double speed = std::sqrt(ball.velocity.lengthSquared());
  if (speed == 0.0) {
    return ball;
  }
  const Vec2 direction = ball.velocity * (1.0 / speed);

  // Constant deceleration along the direction of travel. A ball that stops
  // within the tick covers exactly its remaining rolling distance and rests;
  // otherwise it moves at the average of its start and end speed.
  const double speedLoss = physics.rollingDeceleration * secondsPerTick;
  double endSpeed = 0.0;
  double distance = rollingDistance(speed, physics);
  if (speed > speedLoss) {
    endSpeed = speed - speedLoss;
    distance = (speed + endSpeed) / 2.0 * secondsPerTick;
  }
  const Vec2 end = ball.position + (direction * distance);

  // Out of play: a ball that leaves the pitch stops on the line where it
  // crossed it. The clamp removes rounding that would leave it a hair off the
  // line. A ball already off the pitch (a hand-built state) rolls on.
  if (pitch.contains(ball.position) && !pitch.contains(end)) {
    const double fraction = exitFraction(ball.position, end, pitch);
    BallState stopped = ball;
    stopped.position = pitch.clamp(ball.position + ((end - ball.position) * fraction));
    stopped.velocity = {};
    return stopped;
  }
  BallState moved = ball;
  moved.position = end;
  moved.velocity = direction * endSpeed;
  return moved;
}

MatchSystem makeBallMovementSystem(const BallPhysics& physics, const PassConfig& passing,
                                   const ReceptionConfig& reception,
                                   const RestartConfig& restarts) {
  if (!isValid(physics)) {
    throw std::invalid_argument(
        "ball movement: rolling deceleration must be positive and finite, carry distance finite "
        "and not negative");
  }
  validate(passing);
  validate(reception);
  return {
      .name = std::string(kBallMovementSystemName),
      .update = [physics, passing, reception, restarts](const MatchStepContext& context,
                                                        const MatchState& current,
                                                        MatchStateWriter& next) {
        const double secondsPerTick = context.secondsPerTick();
        // Leaves the ball with this player, at his feet as he ends the tick.
        const auto carryBy = [&](const PlayerMatchState& player) {
          const PlayerMatchState owner =
              ownerAfterMove(player, current.ball().position, secondsPerTick);
          next.setBallPosition(carriedBallPosition(owner, physics, current.pitch()));
          next.setBallVelocity(owner.velocity);
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
                                         .speed = std::sqrt(ball.velocity.lengthSquared())});
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

        // 3. A free ball rolls, and the first player to reach it on its
        //    way takes it -- unless it is already out of play and the
        //    restart system, which runs after this one, is there to settle
        //    who plays on: a player standing on the line must not receive or
        //    intercept the ball first and have the restart overwrite him.
        const BallState rolled = stepFreeBall(ball, physics, current.pitch(), secondsPerTick);
        const bool awaitsRestart = restarts.enabled && isOutOfPlay(ball, current.pitch());
        if (awaitsRestart) {
          return;
        }
        if (const auto claim = findBallClaim(current, ball, rolled.position, context.tick(),
                                             secondsPerTick, reception)) {
          next.setBallOwner(claim->playerId);
          next.setBallLastTouch(BallTouch{.playerId = claim->playerId, .tick = context.tick()});
          next.setLastReception(
              ReceptionRecord{.player = claim->playerId,
                              .tick = context.tick(),
                              .ballSpeed = std::sqrt(ball.velocity.lengthSquared())});
          carryBy(current.players()[claim->playerIndex]);
          const Vec2 contactPosition =
              ball.position + ((rolled.position - ball.position) * claim->contact.contactFraction);
          context.record(controlEvent(current, ball, *claim, contactPosition, context.tick()));
          context.record(PossessionChanged{
              .tick = context.tick(), .previousOwner = std::nullopt, .newOwner = claim->playerId});
          return;
        }
        next.setBallPosition(rolled.position);
        next.setBallVelocity(rolled.velocity);
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
