#include "ballMovement.hpp"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

#include "aerialDuels.hpp"
#include "carrying.hpp"
#include "duel.hpp"
#include "goalFrame.hpp"
#include "matchEvents.hpp"
#include "passCandidates.hpp"
#include "passing.hpp"
#include "playerMovement.hpp"
#include "randomDraws.hpp"
#include "reception.hpp"
#include "restart.hpp"
#include "shooting.hpp"
#include "shotCandidates.hpp"
#include "shotStopping.hpp"
#include "stableMath.hpp"
#include "teamFrame.hpp"
#include "zones.hpp"

namespace ElyverseFootball::SimMatch {
namespace {

using SimCore::PlayerId;
using SimCore::Vec2;

// A touch never errs by so much in pace that pressure could stop it dead or
// turn it round: under full pressure its speed error stays below 1.
constexpr double kMaxTouchSpread = 0.9;

// The owner as the movement system leaves him after this tick: moved and
// turned with the same pure functions, so ball and carrier stay together.
[[nodiscard]] PlayerMatchState ownerAfterMove(const PlayerMatchState& owner, const BallState& ball,
                                              const BallPhysics& physics,
                                              const double secondsPerTick) noexcept {
  const PlayerKinematics moved = stepPlayerMovement(owner, ball, physics, secondsPerTick);
  PlayerMatchState after = owner;
  after.facing = facingAfterMove(owner, moved, ball);
  after.position = moved.position;
  after.velocity = moved.velocity;
  return after;
}

// The vertical speed a lofted pass leaves with, the ball already kicked
// along the ground: the flight that brings it down on its target at the
// intended speed, scaled by the pace error like that speed; 45 degrees where
// the drag stops such a flight short.
[[nodiscard]] double loftedRise(const PassIntent& intent, const BallState& ball,
                                const BallPhysics& physics) noexcept {
  if (!(intent.speed > 0.0)) {
    return 0.0;
  }
  const double distance = SimCore::distance(ball.position, intent.target);
  const double rise =
      launchVerticalVelocity({.speed = intent.speed, .distance = distance, .height = 0.0}, physics)
          .value_or(intent.speed);
  return rise * (ball.velocity.length() / intent.speed);
}

// The pass configuration a touch errs with: both errors errorFactor times a
// pass's, the speed error capped so that full pressure, which spoils it by
// 1 + pressureErrorFactor, keeps it at kMaxTouchSpread.
[[nodiscard]] PassConfig touchConfig(const PassConfig& passing, const double errorFactor) noexcept {
  PassConfig touch = passing;
  touch.directionError = passing.directionError * errorFactor;
  touch.speedError = std::min(passing.speedError * errorFactor,
                              kMaxTouchSpread / (1.0 + passing.pressureErrorFactor));
  return touch;
}

// The passer, if the ball's last touch kicked or headed the last pass. A
// free ball whose last touch did not -- a shot, a deflection or a parry of
// one, a header clear or a keeper's punch -- is loose.
[[nodiscard]] std::optional<PlayerId> passerOf(const BallState& ball,
                                               const std::optional<PassRecord>& pass) noexcept {
  if (!pass || ball.lastTouch != BallTouch{.playerId = pass->passer, .tick = pass->tick}) {
    return std::nullopt;
  }
  return pass->passer;
}

// What gaining control of a free ball was: the ball's last touch passed it,
// so a teammate of his received the pass and an opponent intercepted it. A
// ball nobody played, one that was not passed, or one the passer takes back
// himself, was loose. The pass comes from the step's writer, not from state,
// so a pass played and taken in the same step counts too.
[[nodiscard]] MatchEvent controlEvent(const MatchState& state, const BallState& ball,
                                      const std::optional<PassRecord>& pass, const BallClaim& claim,
                                      const Vec2 contactPosition, const SimCore::SimTick tick) {
  const PlayerMatchState& claimant = state.players()[claim.playerIndex];
  const std::optional<PlayerId> passedBy = passerOf(ball, pass);
  if (!passedBy || *passedBy == claimant.playerId) {
    return LooseBallRecovered{
        .tick = tick, .player = claimant.playerId, .position = contactPosition};
  }
  const PlayerId passer = *passedBy;
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

// The side of the player with this id; he is in the state.
[[nodiscard]] TeamSide sideOf(const MatchState& state, const PlayerId player) {
  return state.players()[findPlayerIndex(state, player).value_or(0)].side;
}

// What a shot that did not go in was: blocked if an outfield player touched
// it, saved if the keeper parried it on its way in, off target otherwise.
[[nodiscard]] ShotOutcome missOutcome(const ShotRecord& shot) noexcept {
  if (shot.deflection) {
    return ShotOutcome::kBlocked;
  }
  return shot.saved ? ShotOutcome::kSaved : ShotOutcome::kOffTarget;
}

// The teammate whose pass the scorer received last, if that is how he came by
// the ball.
[[nodiscard]] std::optional<PlayerId> assistOf(const MatchState& state, const PlayerId scorer) {
  const auto& reception = state.lastReception();
  return reception && reception->player == scorer ? reception->passer : std::nullopt;
}

// A free ball on its way through a tick: where it started, the shot it is, if
// one is still open, and the way it goes -- to the end of the tick, to the
// line it leaves the pitch over, or to the goal frame if it gets to that
// first.
struct FreeBall {
  BallState ball;
  std::optional<ShotRecord> shot;
  BallStep step;
  std::optional<WoodworkHit> frameHit;
};

// The keeper who faces the free ball while it is still in front of him, and
// his answer to its flight: the dive he decided on against it, or standing
// set where he is while he has not reacted yet or left it. A keeper still busy
// with a dive against another flight faces nothing, nor does one off his feet
// after a duel (docs/duels.md#duels-on-the-ball).
struct FacingKeeper {
  std::size_t index = 0;
  ShotRecord shot;
  KeeperDive dive;
  bool dived = false;
};

// One tick of the ball system: the rules it plays by, the step it runs in,
// the state the tick starts from and the writer it leaves the ball in. Each
// method plays its part of moveBall() against these four.
class BallTick {
 public:
  BallTick(const BallMovementRules& rules, const MatchStepContext& context,
           const MatchState& current, MatchStateWriter& next)
      : rules_(&rules), context_(&context), current_(&current), next_(&next) {}

  // The ball's tick: the pending pass or shot, then the ball with its owner or
  // on its own.
  void moveBall() const {
    // 1. Play the pending pass or shot.
    const BallState ball = playPendingAction();

    // 2. A ball touched on rolls ahead of its owner, a controlled one stays
    //    with him. create() and the writer accept no owner outside the state.
    if (ball.touch) {
      moveTouchedBall(ball);
      return;
    }
    if (ball.owner) {
      if (const auto index = findPlayerIndex(current(), *ball.owner)) {
        carryBy(*index, ball);
      }
      return;
    }

    // 3. A free ball moves on -- unless it is already out of play and the
    //    restart system, which runs after this one, is there to settle who
    //    plays on: a player standing on the line must not receive or intercept
    //    the ball first and have the restart overwrite him.
    if (rules().restarts.enabled && isOutOfPlay(ball, current().pitch())) {
      return;
    }
    moveFreeBall(ball);
  }

  // Forgets a keeper's release of the ball from his hands once another player
  // has touched it (hasReleasedBall()): from then on he may take it in them
  // again.
  void forgetReleases() const {
    const auto& touch = next().ball().lastTouch;
    for (std::size_t index = 0; index < current().players().size(); ++index) {
      PlayerTacticalState& tactical = next().tactical(index);
      if (tactical.handsReleased &&
          (!touch || touch->playerId != current().players()[index].playerId)) {
        tactical.handsReleased.reset();
      }
    }
  }

 private:
  // The ball as a pass leaves it: free, at the passer's feet, with the executed
  // pass velocity, and the passer as its last touch. At his feet is where a
  // controlled ball already is -- except when he got it by a command in this
  // very tick, which moves no ball. A ground pass stays on the grass without
  // vertical velocity or spin; a lofted one goes up on loftedRise(), executed
  // with loftedPassConfig().
  [[nodiscard]] BallState kicked(const MatchState& state, const PassIntent& intent,
                                 const BallPhysics& physics, const PassConfig& passing) const {
    BallState ball = state.ball();
    // The passer owns the ball, so he is in the state.
    const auto passer = findPlayerIndex(state, intent.passer).value_or(0);
    const PassConfig kick = intent.lofted ? loftedPassConfig(passing) : passing;
    ball.position = carriedBallPosition(state.players()[passer], physics, state.pitch());
    const bool struckLikeAShot = intent.cross && intent.height > 0.0;
    if (!struckLikeAShot) {
      ball.velocity =
          executePass(intent, ball, state.players()[passer], kick,
                      context().random(SimCore::RandomNumberGeneratorDomain::kExecution),
                      passPressure(state, passer, kick));
    }
    ball.owner = std::nullopt;
    ball.held = false;
    ball.lastTouch = BallTouch{.playerId = intent.passer, .tick = context().tick()};
    ball.height = 0.0;
    ball.verticalVelocity = intent.lofted ? loftedRise(intent, ball, physics) : 0.0;
    ball.spin = 0.0;
    ball.sideSpin = 0.0;
    if (struckLikeAShot) {
      crossed(state, passer, intent, ball);
    }
    return ball;
  }

  // A cross in the air leaves the foot like a shot (docs/on-ball.md#crossing):
  // executeShot()'s flight to its height at its target, along the arc of its
  // curvature, with the crosser's errors and conditions, its aim straying
  // across its way -- the six draws a shot makes, instead of the pass's.
  void crossed(const MatchState& state, const std::size_t passer, const PassIntent& intent,
               BallState& ball) const {
    const ShotIntent strike{.shooter = intent.passer,
                            .target = intent.target,
                            .height = intent.height,
                            .speed = intent.speed,
                            .type = intent.curvature != 0.0 ? ShotType::kCurled : ShotType::kDriven,
                            .curvature = intent.curvature};
    const ShotConditions conditions = shotConditions(
        state, passer, ball.position, strike, context().time(), rules().shooting, rules().passing);
    const PlayerMatchState& crosser = state.players()[passer];
    const SimCore::Vec2 way = SimCore::unitOr(intent.target - ball.position, crosser.facing);
    const ShotStrike struck =
        executeShot(strike, ball.position, crosser,
                    shotErrorFactor(conditions, crosser.attributes, rules().shooting) *
                        rules().shooting.crossErrorShare,
                    rules().shooting, rules().physics,
                    context().random(SimCore::RandomNumberGeneratorDomain::kExecution),
                    {.x = -way.y, .y = way.x});
    ball.velocity = struck.velocity;
    ball.verticalVelocity = struck.verticalVelocity;
    ball.spin = struck.spin;
    ball.sideSpin = struck.sideSpin;
  }

  // The ball as a shot leaves it: free, off the grass at the shooter's feet, on
  // the flight executeShot() strikes it on, and the shooter as its last touch.
  // Records the shot in the state and as an event.
  [[nodiscard]] BallState struck(const MatchState& state, const ShotIntent& intent) const {
    BallState ball = state.ball();
    // The shooter owns the ball, so he is in the state.
    const auto index = findPlayerIndex(state, intent.shooter).value_or(0);
    const PlayerMatchState& shooter = state.players()[index];
    ball.position = carriedBallPosition(shooter, rules().physics, state.pitch());
    const ShotConditions conditions = shotConditions(
        state, index, ball.position, intent, context().time(), rules().shooting, rules().passing);
    const ShotStrike strike = executeShot(
        intent, ball.position, shooter,
        shotErrorFactor(conditions, shooter.attributes, rules().shooting), rules().shooting,
        rules().physics, context().random(SimCore::RandomNumberGeneratorDomain::kExecution));
    ball.velocity = strike.velocity;
    ball.verticalVelocity = strike.verticalVelocity;
    ball.spin = strike.spin;
    ball.sideSpin = strike.sideSpin;
    ball.height = 0.0;
    ball.owner = std::nullopt;
    ball.held = false;
    ball.lastTouch = BallTouch{.playerId = intent.shooter, .tick = context().tick()};

    next().setLastShot(ShotRecord{.shooter = intent.shooter,
                                  .from = ball.position,
                                  .tick = context().tick(),
                                  .deflection = std::nullopt,
                                  .resolved = false});
    const Goal goal = attackedGoal(state.pitch(), shooter.side);
    context().record(ShotAttempted{.tick = context().tick(),
                                   .shooter = intent.shooter,
                                   .from = ball.position,
                                   .target = intent.target,
                                   .height = intent.height,
                                   .struckAt = strike.target,
                                   .struckHeight = strike.height,
                                   .speed = ball.velocity.length(),
                                   .distance = (goal.center - ball.position).length(),
                                   .opening = goalOpening(ball.position, goal),
                                   .type = intent.type});
    return ball;
  }

  // Records what became of the step's open shot, once: the shot is resolved
  // from here on.
  void resolveShot(const ShotRecord& shot, const ShotOutcome outcome) const {
    ShotRecord resolved = shot;
    resolved.resolved = true;
    next().setLastShot(resolved);
    context().record(ShotResolved{.tick = context().tick(),
                                  .shooter = shot.shooter,
                                  .shotTick = shot.tick,
                                  .outcome = outcome});
  }

  // What a shot a player took control of was: blocked if it had come off an
  // outfield player or an outfield opponent took it; saved if the keeper it was
  // meant to beat parried it on its way into his goal before, or took it on its
  // way in; and off target if he took it on its way past, or the shooter's own
  // side got to it first.
  [[nodiscard]] ShotOutcome controlOutcome(const MatchState& state, const ShotRecord& shot,
                                           const BallState& ball, const BallClaim& claim) const {
    const TeamSide shooterSide = sideOf(state, shot.shooter);
    if (shot.deflection) {
      return ShotOutcome::kBlocked;
    }
    if (shot.saved) {
      return ShotOutcome::kSaved;
    }
    if (state.players()[claim.playerIndex].side == shooterSide) {
      return ShotOutcome::kOffTarget;
    }
    if (!isGoalkeeper(state, claim.playerIndex)) {
      return ShotOutcome::kBlocked;
    }
    const GoalEnd end = attackingDirection(shooterSide) > 0.0 ? GoalEnd::kMaxX : GoalEnd::kMinX;
    const auto crossing = predictGoalLineCrossing(ball, rules().physics, state.pitch(), end);
    const bool onTarget =
        crossing && state.pitch().goal(end).framesPoint(crossing->y, crossing->height);
    return onTarget ? ShotOutcome::kSaved : ShotOutcome::kOffTarget;
  }

  // Counts the goal of a ball that crossed this end's goal line inside the
  // frame, and records it: for the side attacking that end, by the shooter of
  // its shot still on its way, otherwise by whoever touched the ball last.
  void scoreGoal(const MatchState& state, const GoalEnd end, const BallState& ball,
                 const std::optional<ShotRecord>& openShot) const {
    const bool homeAttacksMaxX = attackingDirection(TeamSide::kHome) > 0.0;
    const TeamSide side =
        (end == GoalEnd::kMaxX) == homeAttacksMaxX ? TeamSide::kHome : TeamSide::kAway;
    std::optional<PlayerId> scorer;
    if (openShot && sideOf(state, openShot->shooter) == side) {
      scorer = openShot->shooter;
    } else if (ball.lastTouch) {
      scorer = ball.lastTouch->playerId;
    }
    const bool ownGoal = scorer && sideOf(state, *scorer) != side;
    next().addGoal(GoalRecord{.side = side, .scorer = scorer, .tick = context().tick()});
    Score score = state.score();
    (side == TeamSide::kHome ? score.home : score.away) += 1;
    context().record(
        GoalScored{.tick = context().tick(),
                   .side = side,
                   .scorer = scorer,
                   .assist = scorer && !ownGoal ? assistOf(state, *scorer) : std::nullopt,
                   .ownGoal = ownGoal,
                   .score = score});
  }

  // Leaves the ball with the player at this index, at his feet as he ends the
  // tick: on the ground, since that is where a player keeps a ball he has. He
  // moves as the movement system moved him, which saw the ball as `seen`.
  void carryBy(const std::size_t index, const BallState& seen) const {
    const PlayerMatchState owner = ownerAfterMove(
        asMoving(current().players()[index], next().tactical(index), context().tick()), seen,
        rules().physics, context().secondsPerTick());
    next().setBallPosition(carriedBallPosition(owner, rules().physics, current().pitch()));
    next().setBallVelocity(owner.velocity);
    next().setBallHeight(0.0);
    next().setBallVerticalVelocity(0.0);
    next().setBallSpin(0.0);
    next().setBallSideSpin(0.0);
  }

  // Plays the touch: the ball leaves the carrier's feet toward the intent's
  // target at its speed, both off by the touch's error -- executePass() with
  // touchConfig() -- and rolls ahead of him; he keeps it and is its last touch.
  // His run records the touch, and the first touch of a dribble records
  // DribbleAttempted.
  void touchOn(const std::size_t index, const CarryIntent& intent) const {
    const PlayerMatchState& carrier = current().players()[index];
    BallState ball = next().ball();
    ball.position = carriedBallPosition(carrier, rules().physics, current().pitch());
    const PassConfig touch = touchConfig(rules().passing, intent.errorFactor);
    const PassIntent kick{.passer = intent.carrier,
                          .target = intent.target,
                          .speed = intent.speed,
                          .receiver = std::nullopt,
                          .lofted = false};
    next().setBallPosition(ball.position);
    next().setBallVelocity(
        executePass(kick, ball, carrier, touch,
                    context().random(SimCore::RandomNumberGeneratorDomain::kExecution),
                    passPressure(current(), index, touch)));
    next().setBallHeight(0.0);
    next().setBallVerticalVelocity(0.0);
    next().setBallSpin(0.0);
    next().setBallSideSpin(0.0);
    next().setBallTouch(CarryTouch{.tick = context().tick(), .seconds = intent.seconds});
    next().setBallLastTouch(BallTouch{.playerId = intent.carrier, .tick = context().tick()});
    auto& run = next().tactical(index).carry;
    if (!run) {
      return;
    }
    if (run->kind == CarryKind::kDribble && run->dribbling && run->subject &&
        (!run->touchedAt || *run->touchedAt < run->since)) {
      context().record(DribbleAttempted{.tick = context().tick(),
                                        .carrier = intent.carrier,
                                        .opponent = *run->subject,
                                        .from = ball.position,
                                        .target = intent.target});
    }
    run->touchedAt = context().tick();
  }

  // Plays the pending pass, shot or touch, if its player has the ball at his
  // feet or in his hands -- one still rolling ahead of him after a touch is not
  // his to play yet --, and drops every other; returns the ball as that leaves
  // it. The action itself comes from current, not next: one decided this same
  // step is played next step, one step of pending action being deliberate. The
  // ball comes from next, and next.pendingAction() is checked too, because a
  // duel earlier this same step may have already taken the ball and
  // dropped the action, and current would still show the stale pre-step values.
  [[nodiscard]] BallState playPendingAction() const {
    BallState ball = next().ball();
    std::optional<PendingAction> owners;
    std::size_t ownerIndex = 0;
    for (std::size_t index = 0; index < current().players().size(); ++index) {
      const std::optional<PendingAction>& action = current().pendingAction(index);
      if (!action || !next().pendingAction(index)) {
        continue;
      }
      next().setPendingAction(index, std::nullopt);
      if (current().players()[index].playerId == ball.owner && !ball.touch) {
        owners = action;
        ownerIndex = index;
        // Played from his hands, the ball leaves them.
        if (ball.held) {
          next().tactical(index).handsReleased = context().tick();
        }
      }
    }
    if (const ShotIntent* shot = owners ? std::get_if<ShotIntent>(&*owners) : nullptr) {
      ball = struck(current(), *shot);
      next().setBallOwner(ball.owner);
      next().setBallLastTouch(ball.lastTouch);
      context().record(PossessionChanged{
          .tick = context().tick(), .previousOwner = shot->shooter, .newOwner = std::nullopt});
    }
    if (const PassIntent* intent = owners ? std::get_if<PassIntent>(&*owners) : nullptr) {
      ball = kicked(current(), *intent, rules().physics, rules().passing);
      next().setBallOwner(ball.owner);
      next().setBallLastTouch(ball.lastTouch);
      next().setLastPass(PassRecord{.passer = intent->passer,
                                    .from = ball.position,
                                    .tick = context().tick(),
                                    .receiver = intent->receiver});
      context().record(PassAttempted{.tick = context().tick(),
                                     .passer = intent->passer,
                                     .intendedReceiver = intent->receiver,
                                     .from = ball.position,
                                     .target = intent->target,
                                     .speed = ball.velocity.length()});
      if (intent->cross) {
        context().record(CrossAttempted{.tick = context().tick(),
                                        .crosser = intent->passer,
                                        .area = *intent->cross,
                                        .from = ball.position,
                                        .target = intent->target,
                                        .height = intent->height,
                                        .runner = intent->receiver,
                                        .speed = ball.velocity.length()});
      }
      context().record(PossessionChanged{
          .tick = context().tick(), .previousOwner = intent->passer, .newOwner = std::nullopt});
    }
    if (const CarryIntent* carry = owners ? std::get_if<CarryIntent>(&*owners) : nullptr) {
      touchOn(ownerIndex, *carry);
      ball = next().ball();
    }
    return ball;
  }

  [[nodiscard]] FreeBall freeBallOf(const BallState& ball,
                                    const std::optional<ShotRecord>& lastShot, const Pitch& pitch,
                                    const double secondsPerTick) const {
    FreeBall free{.ball = ball,
                  .shot = lastShot && !lastShot->resolved ? lastShot : std::nullopt,
                  .step = stepFreeBallTimed(ball, rules().physics, pitch, secondsPerTick),
                  .frameHit = findWoodworkHit(ball, rules().physics, pitch, rules().woodwork,
                                              secondsPerTick)};
    // A hit beyond the line the ball leaves the pitch over never happens.
    if (free.frameHit && SimCore::distance(ball.position, free.frameHit->ball.position) >
                             SimCore::distance(ball.position, free.step.ball.position)) {
      free.frameHit.reset();
    }
    if (free.frameHit) {
      free.step = {.ball = free.frameHit->ball, .seconds = free.frameHit->seconds};
    }
    return free;
  }

  // Gives the free ball to the player who reached it, as `taker` is when he has
  // it, and returns where he touched it. A goalkeeper who takes it with his
  // hands (hasHands()) holds it, and stands where he took it.
  [[nodiscard]] Vec2 gainBall(const FreeBall& free, const BallClaim& claim,
                              const PlayerMatchState& taker) const {
    const BallState& ball = free.ball;
    next().setBallOwner(claim.playerId);
    next().setBallLastTouch(BallTouch{.playerId = claim.playerId, .tick = context().tick()});
    carryBy(claim.playerIndex, current().ball());
    const Vec2 contactPosition =
        ball.position + ((free.step.ball.position - ball.position) * claim.contact.contactFraction);
    if (hasHands(current(), claim.playerIndex, taker.position, contactPosition)) {
      next().setBallHeld(true);
      next().setPlayerTarget(claim.playerIndex, std::nullopt);
    }
    return contactPosition;
  }

  // Gives the free ball to the player who reached it, as gainBall() does, and
  // records what that was: a pass received or intercepted or a loose ball
  // recovered, and the outcome of a shot.
  void takeBall(const FreeBall& free, const BallClaim& claim, const PlayerMatchState& taker) const {
    const BallState& ball = free.ball;
    const Vec2 contactPosition = gainBall(free, claim, taker);
    const MatchEvent event =
        controlEvent(current(), ball, next().lastPass(), claim, contactPosition, context().tick());
    const auto* received = std::get_if<PassReceived>(&event);
    next().setLastReception(ReceptionRecord{
        .player = claim.playerId,
        .tick = context().tick(),
        .ballSpeed = ball.velocity.length(),
        .passer = received != nullptr ? std::optional(received->passer) : std::nullopt});
    context().record(event);
    context().record(PossessionChanged{
        .tick = context().tick(), .previousOwner = std::nullopt, .newOwner = claim.playerId});
    if (free.shot) {
      resolveShot(*free.shot, controlOutcome(current(), *free.shot, ball, claim));
    }
  }

  // Whether the claim is an outfield opponent of the passer at full stretch
  // for a pass still fast -- standing farther than cleanRadius off the ball's
  // way --: he only gets a touch on it (touchedOff()).
  [[nodiscard]] bool stretchesFor(const BallState& ball, const BallClaim& claim) const {
    const ReceptionConfig& reception = rules().reception;
    const auto passer = passerOf(ball, next().lastPass());
    const double speed = ball.velocity.length();
    if (!passer || speed < reception.deflectSpeed || isGoalkeeper(current(), claim.playerIndex)) {
      return false;
    }
    const PlayerMatchState& claimant = current().players()[claim.playerIndex];
    const Vec2 way = ball.velocity * (1.0 / speed);
    const Vec2 offset = claimant.position - ball.position;
    if (std::abs((way.x * offset.y) - (way.y * offset.x)) <= reception.cleanRadius) {
      return false;
    }
    const auto passerIndex = findPlayerIndex(current(), *passer);
    return passerIndex && current().players()[*passerIndex].side != claimant.side;
  }

  // The pass as it comes off the opponent who stretched for it: deflected as
  // a shot is off a body, with him as its last touch, and loose from there.
  [[nodiscard]] BallState touchedOff(const BallClaim& contact, const FreeBall& free) const {
    const double moment = contact.contact.contactFraction * free.step.seconds;
    BallState ball = deflectShot(ballAfter(free.ball, rules().physics, moment), rules().shooting,
                                 context().random(SimCore::RandomNumberGeneratorDomain::kExecution))
                         .ball;
    ball.lastTouch = BallTouch{.playerId = contact.playerId, .tick = context().tick()};
    next().setBallLastTouch(ball.lastTouch);
    // The pass remembers him: a deflection is no deliberate play (Law 11).
    std::optional<PassRecord> pass = next().lastPass();
    if (pass) {
      pass->deflection = ball.lastTouch;
      next().setLastPass(pass);
    }
    return ball;
  }

  // The shot as it comes off the outfield player who got in its way: he is its
  // last touch, the shot remembers him, and the ball flies on from there.
  [[nodiscard]] BallState deflectedBy(const BallClaim& contact, FreeBall& free) const {
    const double moment = contact.contact.contactFraction * free.step.seconds;
    const Deflection deflection =
        deflectShot(ballAfter(free.ball, rules().physics, moment), rules().shooting,
                    context().random(SimCore::RandomNumberGeneratorDomain::kExecution));
    BallState ball = deflection.ball;
    ball.lastTouch = BallTouch{.playerId = contact.playerId, .tick = context().tick()};
    next().setBallLastTouch(ball.lastTouch);
    // Only an open shot is deflected.
    ShotRecord shot = free.shot.value_or(ShotRecord{});
    shot.deflection = ball.lastTouch;
    free.shot = shot;
    next().setLastShot(shot);
    context().record(ShotDeflected{.tick = context().tick(),
                                   .shooter = shot.shooter,
                                   .shotTick = shot.tick,
                                   .player = contact.playerId,
                                   .position = ball.position,
                                   .height = ball.height,
                                   .blocked = deflection.blocked});
    return ball;
  }

  // Writes the ball as the tick leaves it after flying on from `from`. A ball
  // that left the pitch over a goal line, between the posts and under the
  // crossbar, is a goal; the line stops it flat, so the height it crossed at is
  // asked of the flight. And a shot that left the pitch or came to rest has
  // become what it will be. Returns whether the ball went in.
  // NOLINTNEXTLINE(modernize-use-nodiscard) -- most callers only settle the ball.
  bool settleFreeBall(const BallState& from, const FreeBall& free) const {
    const Pitch& pitch = current().pitch();
    const BallState& moved = free.step.ball;
    next().setBallPosition(moved.position);
    next().setBallVelocity(moved.velocity);
    next().setBallHeight(moved.height);
    next().setBallVerticalVelocity(moved.verticalVelocity);
    next().setBallSpin(moved.spin);
    next().setBallSideSpin(moved.sideSpin);

    const bool left = isOutOfPlay(moved, pitch) && !isOutOfPlay(from, pitch);
    // An indirect free kick nobody else touched scores no goal
    // (docs/set-pieces.md#free-kicks): it is out over the goal line.
    const auto& indirect = current().indirectKick();
    const bool direct =
        indirect && from.lastTouch && from.lastTouch->playerId == indirect->playerId;
    bool goal = false;
    for (const GoalEnd end : {GoalEnd::kMinX, GoalEnd::kMaxX}) {
      if (left && !direct && moved.position.x == pitch.goalLineX(end) &&
          pitch.goal(end).framesPoint(moved.position.y,
                                      ballHeightAfter(from, rules().physics, free.step.seconds))) {
        scoreGoal(current(), end, from, free.shot);
        goal = true;
      }
    }
    if (free.shot && (left || moved.isAtRest())) {
      resolveShot(*free.shot, goal ? ShotOutcome::kGoal : missOutcome(*free.shot));
    }
    return goal;
  }

  [[nodiscard]] std::optional<FacingKeeper> keeperFacing(const FreeBall& free) const {
    const auto index =
        facingKeeper(current(), free.ball, free.shot, rules().physics, rules().saves);
    if (!index || !free.shot || isStumbling(next().tactical(*index), context().tick())) {
      return std::nullopt;
    }
    FacingKeeper keeper{.index = *index, .shot = *free.shot, .dive = {}, .dived = false};
    // A dive decided this very step counts.
    const auto& decided = next().tactical(*index).dive;
    const bool busy = decided && isBusy(*decided, context().time().secondsSince(decided->tick));
    const auto answer =
        busy ? decided
             : standingKeeper(current(), *index, free.ball, context().tick(), rules().saves);
    if (!answer || (busy && !answers(*answer, free.ball))) {
      return std::nullopt;
    }
    keeper.dive = *answer;
    keeper.dived = busy;
    if ((keeper.dive.origin - free.ball.position).dot(keeper.dive.normal) <= 0.0) {
      return std::nullopt;
    }
    return keeper;
  }

  // The free ball passing the keeper who faces it, where it passes his plane:
  // he holds it, it comes off him, or it goes past. The ball's flight began
  // `start` into the tick, and the passage is timed from there. A keeper who
  // stood set is down from then on; one who dived is down as his dive has it.
  // Returns whether that was the ball's tick: held, or off him and on for the
  // rest of the tick; one that went past flies on as if he were not there.
  [[nodiscard]] bool meetKeeper(const FacingKeeper& keeper, const BallPassage& passage,
                                const double start, FreeBall& free) const {
    const KeeperDive& dive = keeper.dive;
    const PlayerMatchState& player = current().players()[keeper.index];
    const double since = context().time().secondsSince(dive.tick);
    const double passed = start + passage.seconds;
    const double feet = feetAt(dive, since + passed, player.attributes);
    const PlanePoint hands = handsAt(dive, since + passed, player.attributes, rules().saves);
    if (!keeper.dived) {
      KeeperDive down = dive;
      down.landSeconds = passed;
      next().tactical(keeper.index).dive = down;
    }
    const ShotRecord& shot = keeper.shot;
    const GoalEnd end = ownGoalEnd(player.side);
    const Goal goal = current().pitch().goal(end);
    const auto crossing =
        predictGoalLineCrossing(free.ball, rules().physics, current().pitch(), end);
    const bool onTarget = crossing && goal.framesPoint(crossing->y, crossing->height);
    const auto record = [&](const SaveResult result) {
      context().record(SaveAttempted{.tick = context().tick(),
                                     .keeper = player.playerId,
                                     .shooter = shot.shooter,
                                     .shotTick = shot.tick,
                                     .result = result,
                                     .position = passage.ball.position,
                                     .height = passage.ball.height});
    };
    if (!touchesKeeper(feet, hands, planePoint(passage.ball, dive), rules().saves)) {
      if (onTarget) {
        record(SaveResult::kOutOfReach);
      }
      return false;
    }

    const SaveTouch touch =
        executeSave(passage.ball, dive, feet, hands, player.attributes, rules().saves,
                    context().random(SimCore::RandomNumberGeneratorDomain::kExecution));
    if (touch.caught) {
      record(SaveResult::kCaught);
      // He has the ball where his dive took him, and is up at once.
      PlayerMatchState taker = player;
      if (keeper.dived) {
        taker.position =
            bodyAt(dive, std::min(since + context().secondsPerTick(), dive.landSeconds),
                   player.attributes, rules().saves);
        taker.velocity = {};
        next().setPlayerPosition(keeper.index, taker.position);
        next().setPlayerVelocity(keeper.index, taker.velocity);
      }
      next().tactical(keeper.index).dive.reset();
      const double share = free.step.seconds > 0.0 ? passage.seconds / free.step.seconds : 0.0;
      takeBall(free,
               {.playerIndex = keeper.index,
                .playerId = player.playerId,
                .contact = {.contactFraction = share, .closestDistance = 0.0}},
               taker);
      // takeBall() carries the ball by its taker's next() move, but the dive has
      // already moved him: the ball lies at his feet where he is.
      if (keeper.dived) {
        next().setBallPosition(carriedBallPosition(taker, rules().physics, current().pitch()));
        next().setBallVelocity(taker.velocity);
      }
      return true;
    }

    BallState from = touch.ball;
    from.lastTouch = BallTouch{.playerId = player.playerId, .tick = context().tick()};
    next().setBallLastTouch(from.lastTouch);
    ShotRecord parried = shot;
    parried.parry = from.lastTouch;
    parried.saved = shot.saved || onTarget;
    free.shot = parried;
    next().setLastShot(parried);
    const auto heading = predictGoalLineCrossing(from, rules().physics, current().pitch(), end);
    const bool behind = heading && !goal.framesPoint(heading->y, heading->height);
    record(behind ? SaveResult::kParriedBehind : SaveResult::kParriedIntoPlay);
    free.step = stepFreeBallTimed(from, rules().physics, current().pitch(),
                                  context().secondsPerTick() - passed);
    settleFreeBall(from, free);
    return true;
  }

  // The ball as it comes off the goal frame, if its flight hits it, and its
  // step on from there for the rest of the tick; the flight began `start` into
  // the tick. A shot that hits the frame records ShotHitWoodwork.
  [[nodiscard]] BallState offTheFrame(FreeBall& free, const double start,
                                      const Pitch& pitch) const {
    if (!free.frameHit) {
      return free.ball;
    }
    const BallState from = free.frameHit->rebound;
    if (free.shot) {
      context().record(ShotHitWoodwork{.tick = context().tick(),
                                       .shooter = free.shot->shooter,
                                       .shotTick = free.shot->tick,
                                       .part = free.frameHit->part,
                                       .position = from.position,
                                       .height = from.height});
    }
    free.step = stepFreeBallTimed(from, rules().physics, pitch,
                                  context().secondsPerTick() - start - free.frameHit->seconds);
    return from;
  }

  // The ball flying on from `from`, `start` into the tick, for the rest of it:
  // the keeper who faces it meets it where it passes him, and it comes off the
  // goal frame, but no other player gets to it before the next tick.
  void flyOn(const BallState& from, const double start, FreeBall& free) const {
    free = freeBallOf(from, free.shot, current().pitch(), context().secondsPerTick() - start);
    if (const auto keeper = keeperFacing(free)) {
      if (const auto passage =
              findPlanePassage(from, keeper->dive, rules().physics, free.step.seconds);
          passage && meetKeeper(*keeper, *passage, start, free)) {
        return;
      }
    }
    const BallState after = offTheFrame(free, start, current().pitch());
    settleFreeBall(after, free);
  }

  // The ball off the head of the winner of an aerial contest -- or a keeper's
  // fists -- as `intent` and his execution of it send it from `there`, the
  // ball where he met it `moment` into the tick. He is its last touch: a shot
  // is a new shot, a pass or a knock-down a pass, a clearance or a punch
  // loose; an open shot he got to has become what it will be. It flies on for
  // the rest of the tick (flyOn()). A header shot came from no reception: a
  // reception of his from before no longer counts, so it earns no assist.
  void playHeader(const HeaderIntent& intent, const BallClaim& claim,
                  const AerialChallenger& winner, const AerialJump& jump, const BallState& there,
                  const double moment, FreeBall& free) const {
    const PlayerMatchState& player = current().players()[winner.playerIndex];
    if (free.shot) {
      resolveShot(*free.shot, controlOutcome(current(), *free.shot, there, claim));
      free.shot.reset();
    }
    HeaderStrike strike =
        executeHeader(intent, there, player,
                      headerErrorFactor(timingSkill(player.attributes, winner.hands), jump.mistime,
                                        rules().aerial),
                      rules().aerial, rules().physics,
                      context().random(SimCore::RandomNumberGeneratorDomain::kExecution));
    // A mistimed header only glances the ball: it keeps some of its own way,
    // never leaving faster than maxHeaderSpeed in all
    // (docs/duels.md#the-header). A keeper's punch is his fist's.
    if (!winner.hands) {
      const Vec2 glanced =
          strike.velocity + (there.velocity * (rules().aerial.glanceShare * jump.mistime));
      const double fastest = rules().aerial.maxHeaderSpeed;
      const double ground = std::sqrt(
          std::max(0.0, (fastest * fastest) - (strike.verticalVelocity * strike.verticalVelocity)));
      const double speed = glanced.length();
      strike.velocity = speed > ground ? glanced * (ground / speed) : glanced;
    }
    BallState from = there;
    from.velocity = strike.velocity;
    from.verticalVelocity = strike.verticalVelocity;
    from.spin = 0.0;
    from.sideSpin = 0.0;
    from.lastTouch = BallTouch{.playerId = player.playerId, .tick = context().tick()};
    next().setBallLastTouch(from.lastTouch);
    if (intent.play == AerialPlay::kShot) {
      const ShotRecord shot{
          .shooter = player.playerId, .from = there.position, .tick = context().tick()};
      next().setLastShot(shot);
      free.shot = shot;
      if (const auto& reception = current().lastReception();
          reception && reception->player == player.playerId) {
        next().setLastReception(std::nullopt);
      }
      const Goal goal = attackedGoal(current().pitch(), player.side);
      context().record(ShotAttempted{.tick = context().tick(),
                                     .shooter = player.playerId,
                                     .from = there.position,
                                     .target = intent.target,
                                     .height = intent.height,
                                     .struckAt = strike.target,
                                     .struckHeight = strike.height,
                                     .speed = strike.velocity.length(),
                                     .distance = (goal.center - there.position).length(),
                                     .opening = goalOpening(there.position, goal),
                                     .type = ShotType::kDriven});
    } else if (intent.receiver) {
      next().setLastPass(PassRecord{.passer = player.playerId,
                                    .from = there.position,
                                    .tick = context().tick(),
                                    .receiver = intent.receiver});
      context().record(PassAttempted{.tick = context().tick(),
                                     .passer = player.playerId,
                                     .intendedReceiver = intent.receiver,
                                     .from = there.position,
                                     .target = intent.target,
                                     .speed = strike.velocity.length()});
    }
    flyOn(from, moment, free);
  }

  // The high ball `first` got to first in this tick: everyone near enough goes
  // up for it and AerialContest records them. Whoever wins it heads it, or, a
  // keeper with his hands, holds it or punches it clear. Everyone who went up
  // is in the air until he lands, and joins `excluded`, which leaves out of
  // the contest who is in it. Returns whether anyone won it, which makes it
  // the ball's tick; nobody reaching it, it flies on as if they were not there.
  [[nodiscard]] bool contestInTheAir(const BallClaim& first, FreeBall& free,
                                     std::vector<std::size_t>& excluded) const {
    const double moment = first.contact.contactFraction * free.step.seconds;
    const BallState there = ballAfter(free.ball, rules().physics, moment);
    const std::vector<AerialChallenger> challengers =
        findChallengers(current(), free.ball, free.step, first, rules().physics, context().time(),
                        rules().reception, rules().aerial, excluded);
    auto& random = context().random(SimCore::RandomNumberGeneratorDomain::kExecution);
    const AerialDuel duel = resolveAerialDuel(current(), challengers, there.height, rules().aerial,
                                              rules().physics.gravity, random);
    AerialContest contest{.tick = context().tick(),
                          .position = there.position,
                          .height = there.height,
                          .contestants = {},
                          .winner = std::nullopt,
                          .play = std::nullopt};
    for (std::size_t index = 0; index < challengers.size(); ++index) {
      const std::size_t playerIndex = challengers[index].playerIndex;
      next().tactical(playerIndex).lastJump = context().tick();
      excluded.push_back(playerIndex);
      contest.contestants.push_back({.player = current().players()[playerIndex].playerId,
                                     .reach = duel.jumps[index].reach,
                                     .reached = duel.jumps[index].reached});
    }
    if (!duel.winner) {
      context().record(contest);
      return false;
    }
    const AerialChallenger& winner = challengers[*duel.winner];
    const PlayerMatchState& player = current().players()[winner.playerIndex];
    const BallClaim claim{
        .playerIndex = winner.playerIndex, .playerId = player.playerId, .contact = first.contact};
    contest.winner = player.playerId;

    HeaderIntent intent;
    if (winner.hands) {
      const bool contested =
          std::ranges::any_of(challengers, [&](const AerialChallenger& challenger) {
            return current().players()[challenger.playerIndex].side != player.side;
          });
      // Drawn always, held or not.
      if (random.nextUniform() < holdChance(player.attributes, contested, rules().aerial)) {
        contest.play = AerialPlay::kCaught;
        context().record(contest);
        takeBall(free, claim, player);
        return true;
      }
      intent = clearanceIntent(current(), winner.playerIndex, there, rules().aerial);
      intent.play = AerialPlay::kPunched;
    } else {
      intent = decideHeader(current(), winner.playerIndex, there, rules().aerial,
                            context().random(SimCore::RandomNumberGeneratorDomain::kAi));
    }
    contest.play = intent.play;
    context().record(contest);
    playHeader(intent, claim, winner, duel.jumps[*duel.winner], there, moment, free);
    return true;
  }

  // The first player a fast shot meets on its way this tick: a body in its
  // way -- the keeper takes it his own way --, or an opponent of the shooter
  // who lunges at it (docs/duels.md#tackles-and-blocks): it comes at him --
  // he is ahead of the ball along its travel --, it would not hit his body
  // anyway, and it passes within lungeRadius, no higher than lungeHeight,
  // before `keeperFraction` of the tick, where it gets to the keeper facing it.
  // His foot lands as a duel's lunge does at full balance (landLunge(), two
  // draws from kExecution) and blocks the shot within footRadius of the ball.
  // Missing, he is off his feet for stumbleSeconds, out of this search, and
  // the shot flies on to whoever is next. Each lunge records TackleAttempted,
  // the shooter as its carrier.
  [[nodiscard]] std::optional<BallClaim> shotContact(const BallState& ball, const FreeBall& free,
                                                     std::vector<std::size_t>& excluded,
                                                     const double keeperFraction) const {
    const ShotConfig& shooting = rules().shooting;
    const ShotRecord shot = free.shot.value_or(ShotRecord{});
    const auto shooter = findPlayerIndex(current(), shot.shooter);
    const double speed = ball.velocity.length();
    const Vec2 travel = speed > 0.0 ? ball.velocity * (1.0 / speed) : Vec2{};
    // Whether the player at this index may lunge at the shot.
    const auto lunges = [&](const std::size_t index) {
      if (!shooter || isGoalkeeper(current(), index) ||
          current().players()[index].side == current().players()[*shooter].side) {
        return false;
      }
      const Vec2 offset = current().players()[index].position - ball.position;
      const double ahead = offset.dot(travel);
      return ahead > 0.0 && (offset - (travel * ahead)).length() > shooting.blockRadius;
    };
    for (;;) {
      const auto body = findBallContact(
          current(), ball, free.step, rules().physics, context().time(), rules().reception,
          {.radius = shooting.blockRadius, .height = shooting.blockReach}, excluded);
      const auto lunger = findFirstReach(
          current(), ball, free.step, rules().physics, context().time(), rules().reception,
          excluded, [&](const std::size_t index) -> std::optional<BallReach> {
            if (!lunges(index)) {
              return std::nullopt;
            }
            return BallReach{.radius = shooting.lungeRadius, .height = shooting.lungeHeight};
          });
      if (!lunger || lunger->contact.contactFraction >= keeperFraction ||
          (body && body->contact.contactFraction <= lunger->contact.contactFraction)) {
        return body;
      }
      const Vec2 ballPoint =
          ballAfter(ball, rules().physics, lunger->contact.contactFraction * free.step.seconds)
              .position;
      const DuelConfig& duels = rules().duels;
      const Vec2 foot =
          landLunge(current().players()[lunger->playerIndex].position, ballPoint, 1.0, duels,
                    context().random(SimCore::RandomNumberGeneratorDomain::kExecution));
      const bool meets = SimCore::distance(foot, ballPoint) <= duels.footRadius;
      context().record(
          TackleAttempted{.tick = context().tick(),
                          .tackler = lunger->playerId,
                          .carrier = shot.shooter,
                          .block = true,
                          .result = meets ? LungeResult::kBlocked : LungeResult::kMissed,
                          .position = ballPoint});
      if (meets) {
        return lunger;
      }
      next().tactical(lunger->playerIndex).stumbledUntil =
          stumbleEnd(context().time(), duels.stumbleSeconds);
      excluded.push_back(lunger->playerIndex);
    }
  }

  // The share of the tick after which the ball gets to the keeper facing it,
  // 1 if it does not: nobody lunges at a shot beyond it.
  [[nodiscard]] double keeperFractionOf(const std::optional<FacingKeeper>& keeper,
                                        const BallState& ball, const FreeBall& free) const {
    if (!keeper || !(free.step.seconds > 0.0)) {
      return 1.0;
    }
    const auto reached = findPlanePassage(ball, keeper->dive, rules().physics, free.step.seconds);
    return reached ? reached->seconds / free.step.seconds : 1.0;
  }

  // Adds everyone off his feet after a duel to those who take no ball -- one
  // who stumbled earlier this same step too (docs/duels.md#duels-on-the-ball).
  void excludeStumbling(std::vector<std::size_t>& excluded) const {
    for (std::size_t index = 0; index < current().players().size(); ++index) {
      if (isStumbling(next().tactical(index), context().tick())) {
        excluded.push_back(index);
      }
    }
  }

  // A free ball rolls, flies and bounces, and the first player to reach it on
  // its way takes it. A shot still fast comes off an outfield player's body
  // rather than to his feet, the keeper it faces meets it where it passes him,
  // a high ball is contested in the air, and any ball comes off the goal
  // frame; either way it flies on for the rest of the tick, and nothing else
  // gets to it before the next. A high ball nobody won flies on past everyone
  // who went up for it, to the keeper and to whoever else gets to it.
  void moveFreeBall(const BallState& ball) const {
    const Pitch& pitch = current().pitch();
    const double secondsPerTick = context().secondsPerTick();
    // The shot comes from the step's writer: one struck this step counts.
    FreeBall free = freeBallOf(ball, next().lastShot(), pitch, secondsPerTick);

    const auto keeper = keeperFacing(free);
    // The keeper facing the ball meets it his own way.
    std::vector<std::size_t> excluded;
    if (keeper) {
      excluded.push_back(keeper->index);
    }
    excludeStumbling(excluded);
    const bool deflects = free.shot && ball.velocity.length() >= rules().shooting.deflectionSpeed;
    const auto contactOf = [&] {
      return deflects ? shotContact(ball, free, excluded, keeperFractionOf(keeper, ball, free))
                      : findBallClaim(current(), ball, free.step, rules().physics, context().time(),
                                      rules().reception, excluded);
    };
    auto contact = contactOf();
    // A high ball someone gets to before anyone takes it at his feet is
    // contested in the air; a fast shot comes off a body instead.
    const auto aerial =
        deflects ? std::nullopt
                 : findAerialContact(current(), ball, free.step, rules().physics, context().time(),
                                     rules().reception, rules().aerial, excluded);
    const bool inTheAir =
        aerial && (!contact || aerial->contact.contactFraction <= contact->contact.contactFraction);
    // The keeper meets the ball if it passes him before anyone else gets to
    // it, at most once: a ball that went past him flies on.
    std::optional<BallPassage> passage;
    const auto meetsKeeper = [&](const std::optional<BallClaim>& first) {
      if (!keeper || passage) {
        return false;
      }
      passage =
          findPlanePassage(ball, keeper->dive, rules().physics,
                           free.step.seconds * (first ? first->contact.contactFraction : 1.0));
      return passage && meetKeeper(*keeper, *passage, 0.0, free);
    };
    if (meetsKeeper(inTheAir ? aerial : contact)) {
      return;
    }
    if (inTheAir) {
      if (contestInTheAir(*aerial, free, excluded)) {
        return;
      }
      // Who went up for it does not take it at his feet as well.
      contact = contactOf();
      if (meetsKeeper(contact)) {
        return;
      }
    }
    if (contact && !deflects && stretchesFor(ball, *contact)) {
      const double moment = contact->contact.contactFraction * free.step.seconds;
      const BallState from = touchedOff(*contact, free);
      free.step = stepFreeBallTimed(from, rules().physics, pitch, secondsPerTick - moment);
      settleFreeBall(from, free);
      return;
    }
    if (contact && (!deflects || isGoalkeeper(current(), contact->playerIndex))) {
      takeBall(free, *contact, current().players()[contact->playerIndex]);
      return;
    }

    BallState from = ball;
    if (contact) {
      const double moment = contact->contact.contactFraction * free.step.seconds;
      from = deflectedBy(*contact, free);
      free.step = stepFreeBallTimed(from, rules().physics, pitch, secondsPerTick - moment);
    } else {
      from = offTheFrame(free, 0.0, pitch);
    }
    settleFreeBall(from, free);
  }

  // Ends the open dribble of the run of the player at this index, if any:
  // records DribbleEnded with this outcome, where the ball is.
  void endDribble(const std::size_t index, const DribbleOutcome outcome) const {
    auto& run = next().tactical(index).carry;
    if (!run || !run->dribbling || !run->subject) {
      return;
    }
    run->dribbling = false;
    context().record(DribbleEnded{.tick = context().tick(),
                                  .carrier = current().players()[index].playerId,
                                  .opponent = *run->subject,
                                  .outcome = outcome,
                                  .position = next().ball().position});
  }

  // The carrier has his touched ball at his feet again, as he ends the tick.
  // A dribble is over: he beat the opponent if he is now nearer the goal he
  // attacks than him, along the attacking direction, otherwise he kept the ball
  // without getting past.
  void retake(const std::size_t index) const {
    const PlayerMatchState& carrier = current().players()[index];
    next().setBallTouch(std::nullopt);
    carryBy(index, current().ball());
    auto& run = next().tactical(index).carry;
    if (!run) {
      return;
    }
    run->retakenAt = context().tick();
    if (!run->dribbling || !run->subject) {
      return;
    }
    const auto opponent = findPlayerIndex(current(), *run->subject);
    const double forward = attackingDirection(carrier.side);
    const bool past = opponent && forward * next().ball().position.x >
                                      forward * current().players()[*opponent].position.x;
    endDribble(index, past ? DribbleOutcome::kBeaten : DribbleOutcome::kKept);
  }

  // An opponent got to the ball rolling ahead of its owner: he has it now, at
  // his feet or in his hands, and the owner has lost it -- BallWon, by a loose
  // touch.
  void loseTouch(const FreeBall& free, const BallClaim& claim, const std::size_t ownerIndex) const {
    const PlayerId owner = current().players()[ownerIndex].playerId;
    const Vec2 contactPosition = gainBall(free, claim, current().players()[claim.playerIndex]);
    next().setLastReception(ReceptionRecord{.player = claim.playerId,
                                            .tick = context().tick(),
                                            .ballSpeed = free.ball.velocity.length(),
                                            .passer = std::nullopt});
    context().record(BallWon{.tick = context().tick(),
                             .winner = claim.playerId,
                             .loser = owner,
                             .position = contactPosition,
                             .cause = BallWonCause::kLooseTouch});
    context().record(PossessionChanged{
        .tick = context().tick(), .previousOwner = owner, .newOwner = claim.playerId});
    endDribble(ownerIndex, DribbleOutcome::kLost);
  }

  // The ball rolling ahead of its owner after a touch
  // (docs/on-ball.md#ball-carrying): it rolls as a free ball does, and whoever
  // reaches it first takes it -- the owner once the touch's seconds have passed,
  // without waiting for the reclaim delay, which is for passes, and any opponent;
  // his teammates leave it to him. Nobody reaching it, it rolls on, off the goal
  // frame and into the goal or out of play like any ball; one out of play is
  // nobody's.
  void moveTouchedBall(const BallState& ball) const {
    const auto ownerIndex = findPlayerIndex(current(), ball.owner.value_or(PlayerId::invalid()));
    if (!ownerIndex || !ball.touch) {
      return;
    }
    const PlayerMatchState& owner = current().players()[*ownerIndex];
    const bool reachable = ball.touch->hasPassed(context().time());
    std::vector<std::size_t> excluded;
    for (std::size_t index = 0; index < current().players().size(); ++index) {
      const bool teammate = index != *ownerIndex && current().players()[index].side == owner.side;
      if (teammate || (index == *ownerIndex && !reachable)) {
        excluded.push_back(index);
      }
    }
    excludeStumbling(excluded);
    // It rolls, leaves the pitch and goes in as a ball of nobody's would. Who
    // gets to it is asked with its owner, who runs as fast as the ball lets
    // him, but without its last touch: his own touch does not keep him off it.
    BallState loose = ball;
    loose.owner.reset();
    loose.touch.reset();
    FreeBall free = freeBallOf(loose, std::nullopt, current().pitch(), context().secondsPerTick());
    BallState rolling = ball;
    rolling.lastTouch.reset();
    if (const auto claim = findBallClaim(current(), rolling, free.step, rules().physics,
                                         context().time(), rules().reception, excluded)) {
      if (claim->playerIndex == *ownerIndex) {
        retake(*ownerIndex);
      } else {
        loseTouch(free, *claim, *ownerIndex);
      }
      return;
    }
    const BallState from = offTheFrame(free, 0.0, current().pitch());
    const bool goal = settleFreeBall(from, free);
    BallState after = next().ball();
    after.owner.reset();
    if (isOutOfPlay(after, current().pitch())) {
      next().setBallOwner(std::nullopt);
      context().record(PossessionChanged{
          .tick = context().tick(), .previousOwner = owner.playerId, .newOwner = std::nullopt});
      // A dribble whose touch went in has not lost the ball.
      if (!goal) {
        endDribble(*ownerIndex, DribbleOutcome::kLost);
      }
    }
  }

  [[nodiscard]] const BallMovementRules& rules() const noexcept { return *rules_; }
  [[nodiscard]] const MatchStepContext& context() const noexcept { return *context_; }
  [[nodiscard]] const MatchState& current() const noexcept { return *current_; }
  [[nodiscard]] MatchStateWriter& next() const noexcept { return *next_; }

  const BallMovementRules* rules_;
  const MatchStepContext* context_;
  const MatchState* current_;
  MatchStateWriter* next_;
};

}  // namespace

MatchSystem makeBallMovementSystem(const BallMovementRules& rules) {
  validate(rules.physics);
  validate(rules.passing);
  validate(rules.reception);
  validate(rules.shooting);
  validate(rules.woodwork);
  validate(rules.saves);
  validate(rules.aerial);
  return {.name = std::string(kBallMovementSystemName),
          .update = [rules](const MatchStepContext& context, const MatchState& current,
                            MatchStateWriter& next) {
            // A dead ball lies still and nobody plays it (docs/set-pieces.md).
            if (next.setPiece()) {
              return;
            }
            const BallTick tick(rules, context, current, next);
            tick.moveBall();
            tick.forgetReleases();
          }};
}

}  // namespace ElyverseFootball::SimMatch
