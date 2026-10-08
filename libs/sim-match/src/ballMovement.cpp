#include "ballMovement.hpp"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

#include "aerialDuels.hpp"
#include "goalFrame.hpp"
#include "matchEvents.hpp"
#include "passCandidates.hpp"
#include "passing.hpp"
#include "playerMovement.hpp"
#include "reception.hpp"
#include "restart.hpp"
#include "shooting.hpp"
#include "shotCandidates.hpp"
#include "shotStopping.hpp"
#include "teamFrame.hpp"
#include "zones.hpp"

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

// The ball as a pass leaves it: free, at the passer's feet, with the executed
// pass velocity, and the passer as its last touch. At his feet is where a
// controlled ball already is -- except when he got it by a command in this
// very tick, which moves no ball. A ground pass stays on the grass without
// vertical velocity or spin; a lofted one goes up on loftedRise(), executed
// with loftedPassConfig().
[[nodiscard]] BallState kicked(const MatchState& state, const PassIntent& intent,
                               const BallPhysics& physics, const PassConfig& passing,
                               const MatchStepContext& context) {
  BallState ball = state.ball();
  // The passer owns the ball, so he is in the state.
  const auto passer = findPlayerIndex(state, intent.passer).value_or(0);
  const PassConfig kick = intent.lofted ? loftedPassConfig(passing) : passing;
  ball.position = carriedBallPosition(state.players()[passer], physics, state.pitch());
  ball.velocity = executePass(intent, ball, state.players()[passer], kick,
                              context.random(SimCore::RandomNumberGeneratorDomain::kExecution),
                              passPressure(state, passer, kick));
  ball.owner = std::nullopt;
  ball.held = false;
  ball.lastTouch = BallTouch{.playerId = intent.passer, .tick = context.tick()};
  ball.height = 0.0;
  ball.verticalVelocity = intent.lofted ? loftedRise(intent, ball, physics) : 0.0;
  ball.spin = 0.0;
  return ball;
}

// What the ball system is configured with.
struct BallRules {
  BallPhysics physics;
  PassConfig passing;
  ReceptionConfig reception;
  RestartConfig restarts;
  ShotConfig shooting;
  WoodworkConfig woodwork;
  ShotStoppingConfig saves;
  AerialConfig aerial;
};

// The ball as a shot leaves it: free, off the grass at the shooter's feet, on
// the flight executeShot() strikes it on, and the shooter as its last touch.
// Records the shot in the state and as an event.
[[nodiscard]] BallState struck(const MatchState& state, const ShotIntent& intent,
                               const BallRules& rules, const MatchStepContext& context,
                               MatchStateWriter& next) {
  BallState ball = state.ball();
  // The shooter owns the ball, so he is in the state.
  const auto index = findPlayerIndex(state, intent.shooter).value_or(0);
  const PlayerMatchState& shooter = state.players()[index];
  ball.position = carriedBallPosition(shooter, rules.physics, state.pitch());
  const ShotConditions conditions =
      shotConditions(state, index, ball.position, intent, context.tick(), context.secondsPerTick(),
                     rules.shooting, rules.passing);
  const ShotStrike strike =
      executeShot(intent, ball.position, shooter,
                  shotErrorFactor(conditions, shooter.attributes, rules.shooting), rules.shooting,
                  rules.physics, context.random(SimCore::RandomNumberGeneratorDomain::kExecution));
  ball.velocity = strike.velocity;
  ball.verticalVelocity = strike.verticalVelocity;
  ball.spin = strike.spin;
  ball.height = 0.0;
  ball.owner = std::nullopt;
  ball.held = false;
  ball.lastTouch = BallTouch{.playerId = intent.shooter, .tick = context.tick()};

  next.setLastShot(ShotRecord{.shooter = intent.shooter,
                              .from = ball.position,
                              .tick = context.tick(),
                              .deflection = std::nullopt,
                              .resolved = false});
  const Goal goal = attackedGoal(state.pitch(), shooter.side);
  context.record(ShotAttempted{.tick = context.tick(),
                               .shooter = intent.shooter,
                               .from = ball.position,
                               .target = intent.target,
                               .height = intent.height,
                               .struckAt = strike.target,
                               .struckHeight = strike.height,
                               .speed = ball.velocity.length(),
                               .distance = (goal.center - ball.position).length(),
                               .opening = goalOpening(ball.position, goal)});
  return ball;
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

// Records what became of the step's open shot, once: the shot is resolved
// from here on.
void resolveShot(const ShotRecord& shot, const ShotOutcome outcome, const MatchStepContext& context,
                 MatchStateWriter& next) {
  ShotRecord resolved = shot;
  resolved.resolved = true;
  next.setLastShot(resolved);
  context.record(ShotResolved{
      .tick = context.tick(), .shooter = shot.shooter, .shotTick = shot.tick, .outcome = outcome});
}

// What a shot that did not go in was: blocked if an outfield player touched
// it, saved if the keeper parried it on its way in, off target otherwise.
[[nodiscard]] ShotOutcome missOutcome(const ShotRecord& shot) noexcept {
  if (shot.deflection) {
    return ShotOutcome::kBlocked;
  }
  return shot.saved ? ShotOutcome::kSaved : ShotOutcome::kOffTarget;
}

// What a shot a player took control of was: blocked if it had come off an
// outfield player or an outfield opponent took it; saved if the keeper it was
// meant to beat parried it on its way into his goal before, or took it on its
// way in; and off target if he took it on its way past, or the shooter's own
// side got to it first.
[[nodiscard]] ShotOutcome controlOutcome(const MatchState& state, const ShotRecord& shot,
                                         const BallState& ball, const BallClaim& claim,
                                         const BallRules& rules) {
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
  const auto crossing = predictGoalLineCrossing(ball, rules.physics, state.pitch(), end);
  const bool onTarget =
      crossing && state.pitch().goal(end).framesPoint(crossing->y, crossing->height);
  return onTarget ? ShotOutcome::kSaved : ShotOutcome::kOffTarget;
}

// The teammate whose pass the scorer received last, if that is how he came by
// the ball.
[[nodiscard]] std::optional<PlayerId> assistOf(const MatchState& state, const PlayerId scorer) {
  const auto& reception = state.lastReception();
  return reception && reception->player == scorer ? reception->passer : std::nullopt;
}

// Counts the goal of a ball that crossed this end's goal line inside the
// frame, and records it: for the side attacking that end, by the shooter of
// its shot still on its way, otherwise by whoever touched the ball last.
void scoreGoal(const MatchState& state, const GoalEnd end, const BallState& ball,
               const std::optional<ShotRecord>& openShot, const MatchStepContext& context,
               MatchStateWriter& next) {
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
  next.addGoal(GoalRecord{.side = side, .scorer = scorer, .tick = context.tick()});
  Score score = state.score();
  (side == TeamSide::kHome ? score.home : score.away) += 1;
  context.record(GoalScored{.tick = context.tick(),
                            .side = side,
                            .scorer = scorer,
                            .assist = scorer && !ownGoal ? assistOf(state, *scorer) : std::nullopt,
                            .ownGoal = ownGoal,
                            .score = score});
}

// Leaves the ball with this player, at his feet as he ends the tick: on the
// ground, since that is where a player keeps a ball he has.
void carryBy(const PlayerMatchState& player, const BallRules& rules,
             const MatchStepContext& context, const MatchState& current, MatchStateWriter& next) {
  const PlayerMatchState owner =
      ownerAfterMove(player, current.ball().position, context.secondsPerTick());
  next.setBallPosition(carriedBallPosition(owner, rules.physics, current.pitch()));
  next.setBallVelocity(owner.velocity);
  next.setBallHeight(0.0);
  next.setBallVerticalVelocity(0.0);
  next.setBallSpin(0.0);
}

// Plays the pending pass or shot, if its player owns the ball, and drops
// every other; returns the ball as that leaves it. The action itself comes
// from current, not next: one decided this same step is played next step, one
// step of pending action being deliberate. The ball comes from next, and
// next.pendingAction() is checked too, because a challenge earlier this same
// step may have already taken the ball and dropped the action, and current
// would still show the stale pre-step values.
[[nodiscard]] BallState playPendingAction(const BallRules& rules, const MatchStepContext& context,
                                          const MatchState& current, MatchStateWriter& next) {
  BallState ball = next.ball();
  std::optional<PendingAction> owners;
  for (std::size_t index = 0; index < current.players().size(); ++index) {
    const std::optional<PendingAction>& action = current.pendingAction(index);
    if (!action || !next.pendingAction(index)) {
      continue;
    }
    next.setPendingAction(index, std::nullopt);
    if (current.players()[index].playerId == ball.owner) {
      owners = action;
      // Played from his hands, the ball leaves them.
      if (ball.held) {
        next.tactical(index).handsReleased = context.tick();
      }
    }
  }
  if (const ShotIntent* shot = owners ? std::get_if<ShotIntent>(&*owners) : nullptr) {
    ball = struck(current, *shot, rules, context, next);
    next.setBallOwner(ball.owner);
    next.setBallLastTouch(ball.lastTouch);
    context.record(PossessionChanged{
        .tick = context.tick(), .previousOwner = shot->shooter, .newOwner = std::nullopt});
  }
  if (const PassIntent* intent = owners ? std::get_if<PassIntent>(&*owners) : nullptr) {
    ball = kicked(current, *intent, rules.physics, rules.passing, context);
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
  return ball;
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

[[nodiscard]] FreeBall freeBallOf(const BallState& ball, const std::optional<ShotRecord>& lastShot,
                                  const BallRules& rules, const Pitch& pitch,
                                  const double secondsPerTick) {
  FreeBall free{
      .ball = ball,
      .shot = lastShot && !lastShot->resolved ? lastShot : std::nullopt,
      .step = stepFreeBallTimed(ball, rules.physics, pitch, secondsPerTick),
      .frameHit = findWoodworkHit(ball, rules.physics, pitch, rules.woodwork, secondsPerTick)};
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
// it, and records what that was: a pass received or intercepted or a loose
// ball recovered, and the outcome of a shot. A goalkeeper who takes it with
// his hands (hasHands()) holds it, and stands where he took it.
void takeBall(const FreeBall& free, const BallClaim& claim, const PlayerMatchState& taker,
              const BallRules& rules, const MatchStepContext& context, const MatchState& current,
              MatchStateWriter& next) {
  const BallState& ball = free.ball;
  next.setBallOwner(claim.playerId);
  next.setBallLastTouch(BallTouch{.playerId = claim.playerId, .tick = context.tick()});
  carryBy(taker, rules, context, current, next);
  const Vec2 contactPosition =
      ball.position + ((free.step.ball.position - ball.position) * claim.contact.contactFraction);
  if (hasHands(current, claim.playerIndex, taker.position, contactPosition)) {
    next.setBallHeld(true);
    next.setPlayerTarget(claim.playerIndex, std::nullopt);
  }
  const MatchEvent event =
      controlEvent(current, ball, next.lastPass(), claim, contactPosition, context.tick());
  const auto* received = std::get_if<PassReceived>(&event);
  next.setLastReception(ReceptionRecord{
      .player = claim.playerId,
      .tick = context.tick(),
      .ballSpeed = ball.velocity.length(),
      .passer = received != nullptr ? std::optional(received->passer) : std::nullopt});
  context.record(event);
  context.record(PossessionChanged{
      .tick = context.tick(), .previousOwner = std::nullopt, .newOwner = claim.playerId});
  if (free.shot) {
    resolveShot(*free.shot, controlOutcome(current, *free.shot, ball, claim, rules), context, next);
  }
}

// The shot as it comes off the outfield player who got in its way: he is its
// last touch, the shot remembers him, and the ball flies on from there.
[[nodiscard]] BallState deflectedBy(const BallClaim& contact, FreeBall& free,
                                    const BallRules& rules, const MatchStepContext& context,
                                    MatchStateWriter& next) {
  const double moment = contact.contact.contactFraction * free.step.seconds;
  const Deflection deflection =
      deflectShot(ballAfter(free.ball, rules.physics, moment), rules.shooting,
                  context.random(SimCore::RandomNumberGeneratorDomain::kExecution));
  BallState ball = deflection.ball;
  ball.lastTouch = BallTouch{.playerId = contact.playerId, .tick = context.tick()};
  next.setBallLastTouch(ball.lastTouch);
  // Only an open shot is deflected.
  ShotRecord shot = free.shot.value_or(ShotRecord{});
  shot.deflection = ball.lastTouch;
  free.shot = shot;
  next.setLastShot(shot);
  context.record(ShotDeflected{.tick = context.tick(),
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
// become what it will be.
void settleFreeBall(const BallState& from, const FreeBall& free, const BallRules& rules,
                    const MatchStepContext& context, const MatchState& current,
                    MatchStateWriter& next) {
  const Pitch& pitch = current.pitch();
  const BallState& moved = free.step.ball;
  next.setBallPosition(moved.position);
  next.setBallVelocity(moved.velocity);
  next.setBallHeight(moved.height);
  next.setBallVerticalVelocity(moved.verticalVelocity);
  next.setBallSpin(moved.spin);

  const bool left = isOutOfPlay(moved, pitch) && !isOutOfPlay(from, pitch);
  bool goal = false;
  for (const GoalEnd end : {GoalEnd::kMinX, GoalEnd::kMaxX}) {
    if (left && moved.position.x == pitch.goalLineX(end) &&
        pitch.goal(end).framesPoint(moved.position.y,
                                    ballHeightAfter(from, rules.physics, free.step.seconds))) {
      scoreGoal(current, end, from, free.shot, context, next);
      goal = true;
    }
  }
  if (free.shot && (left || moved.isAtRest())) {
    resolveShot(*free.shot, goal ? ShotOutcome::kGoal : missOutcome(*free.shot), context, next);
  }
}

// The keeper who faces the free ball while it is still in front of him, and
// his answer to its flight: the dive he decided on against it, or standing
// set where he is while he has not reacted yet or left it. A keeper still busy
// with a dive against another flight faces nothing.
struct FacingKeeper {
  std::size_t index = 0;
  ShotRecord shot;
  KeeperDive dive;
  bool dived = false;
};

[[nodiscard]] std::optional<FacingKeeper> keeperFacing(const FreeBall& free, const BallRules& rules,
                                                       const MatchStepContext& context,
                                                       const MatchState& current,
                                                       MatchStateWriter& next) {
  const auto index = facingKeeper(current, free.ball, free.shot, rules.physics, rules.saves);
  if (!index || !free.shot) {
    return std::nullopt;
  }
  FacingKeeper keeper{.index = *index, .shot = *free.shot, .dive = {}, .dived = false};
  // A dive decided this very step counts.
  const auto& decided = next.tactical(*index).dive;
  const bool busy =
      decided &&
      isBusy(*decided, static_cast<double>(context.tick().value() - decided->tick.value()) *
                           context.secondsPerTick());
  const auto answer =
      busy ? decided : standingKeeper(current, *index, free.ball, context.tick(), rules.saves);
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
                              const double start, FreeBall& free, const BallRules& rules,
                              const MatchStepContext& context, const MatchState& current,
                              MatchStateWriter& next) {
  const KeeperDive& dive = keeper.dive;
  const PlayerMatchState& player = current.players()[keeper.index];
  const double since =
      static_cast<double>(context.tick().value() - dive.tick.value()) * context.secondsPerTick();
  const double passed = start + passage.seconds;
  const double feet = feetAt(dive, since + passed, player.attributes);
  const PlanePoint hands = handsAt(dive, since + passed, player.attributes, rules.saves);
  if (!keeper.dived) {
    KeeperDive down = dive;
    down.landSeconds = passed;
    next.tactical(keeper.index).dive = down;
  }
  const ShotRecord& shot = keeper.shot;
  const GoalEnd end = ownGoalEnd(player.side);
  const Goal goal = current.pitch().goal(end);
  const auto crossing = predictGoalLineCrossing(free.ball, rules.physics, current.pitch(), end);
  const bool onTarget = crossing && goal.framesPoint(crossing->y, crossing->height);
  const auto record = [&](const SaveResult result) {
    context.record(SaveAttempted{.tick = context.tick(),
                                 .keeper = player.playerId,
                                 .shooter = shot.shooter,
                                 .shotTick = shot.tick,
                                 .result = result,
                                 .position = passage.ball.position,
                                 .height = passage.ball.height});
  };
  if (!touchesKeeper(feet, hands, planePoint(passage.ball, dive), rules.saves)) {
    if (onTarget) {
      record(SaveResult::kOutOfReach);
    }
    return false;
  }

  const SaveTouch touch =
      executeSave(passage.ball, dive, feet, hands, player.attributes, rules.saves,
                  context.random(SimCore::RandomNumberGeneratorDomain::kExecution));
  if (touch.caught) {
    record(SaveResult::kCaught);
    // He has the ball where his dive took him, and is up at once.
    PlayerMatchState taker = player;
    if (keeper.dived) {
      taker.position = bodyAt(dive, std::min(since + context.secondsPerTick(), dive.landSeconds),
                              player.attributes, rules.saves);
      taker.velocity = {};
      next.setPlayerPosition(keeper.index, taker.position);
      next.setPlayerVelocity(keeper.index, taker.velocity);
    }
    next.tactical(keeper.index).dive.reset();
    const double share = free.step.seconds > 0.0 ? passage.seconds / free.step.seconds : 0.0;
    takeBall(free,
             {.playerIndex = keeper.index,
              .playerId = player.playerId,
              .contact = {.contactFraction = share, .closestDistance = 0.0}},
             taker, rules, context, current, next);
    // takeBall() carries the ball by its taker's next move, but the dive has
    // already moved him: the ball lies at his feet where he is.
    if (keeper.dived) {
      next.setBallPosition(carriedBallPosition(taker, rules.physics, current.pitch()));
      next.setBallVelocity(taker.velocity);
    }
    return true;
  }

  BallState from = touch.ball;
  from.lastTouch = BallTouch{.playerId = player.playerId, .tick = context.tick()};
  next.setBallLastTouch(from.lastTouch);
  ShotRecord parried = shot;
  parried.parry = from.lastTouch;
  parried.saved = shot.saved || onTarget;
  free.shot = parried;
  next.setLastShot(parried);
  const auto heading = predictGoalLineCrossing(from, rules.physics, current.pitch(), end);
  const bool behind = heading && !goal.framesPoint(heading->y, heading->height);
  record(behind ? SaveResult::kParriedBehind : SaveResult::kParriedIntoPlay);
  free.step =
      stepFreeBallTimed(from, rules.physics, current.pitch(), context.secondsPerTick() - passed);
  settleFreeBall(from, free, rules, context, current, next);
  return true;
}

// The ball as it comes off the goal frame, if its flight hits it, and its
// step on from there for the rest of the tick; the flight began `start` into
// the tick. A shot that hits the frame records ShotHitWoodwork.
[[nodiscard]] BallState offTheFrame(FreeBall& free, const double start, const BallRules& rules,
                                    const MatchStepContext& context, const Pitch& pitch) {
  if (!free.frameHit) {
    return free.ball;
  }
  const BallState from = free.frameHit->rebound;
  if (free.shot) {
    context.record(ShotHitWoodwork{.tick = context.tick(),
                                   .shooter = free.shot->shooter,
                                   .shotTick = free.shot->tick,
                                   .part = free.frameHit->part,
                                   .position = from.position,
                                   .height = from.height});
  }
  free.step = stepFreeBallTimed(from, rules.physics, pitch,
                                context.secondsPerTick() - start - free.frameHit->seconds);
  return from;
}

// The ball flying on from `from`, `start` into the tick, for the rest of it:
// the keeper who faces it meets it where it passes him, and it comes off the
// goal frame, but no other player gets to it before the next tick.
void flyOn(const BallState& from, const double start, FreeBall& free, const BallRules& rules,
           const MatchStepContext& context, const MatchState& current, MatchStateWriter& next) {
  free = freeBallOf(from, free.shot, rules, current.pitch(), context.secondsPerTick() - start);
  if (const auto keeper = keeperFacing(free, rules, context, current, next)) {
    if (const auto passage = findPlanePassage(from, keeper->dive, rules.physics, free.step.seconds);
        passage && meetKeeper(*keeper, *passage, start, free, rules, context, current, next)) {
      return;
    }
  }
  const BallState after = offTheFrame(free, start, rules, context, current.pitch());
  settleFreeBall(after, free, rules, context, current, next);
}

// The ball off the head of the winner of an aerial contest -- or a keeper's
// fists -- as `intent` and his execution of it send it from `there`, the
// ball where he met it `moment` into the tick. He is its last touch: a shot
// is a new shot, a pass or a knock-down a pass, a clearance or a punch
// loose; an open shot he got to has become what it will be. It flies on for
// the rest of the tick (flyOn()). A header shot came from no reception: a
// reception of his from before no longer counts, so it earns no assist.
void playHeader(const HeaderIntent& intent, const BallClaim& claim, const AerialChallenger& winner,
                const AerialJump& jump, const BallState& there, const double moment, FreeBall& free,
                const BallRules& rules, const MatchStepContext& context, const MatchState& current,
                MatchStateWriter& next) {
  const PlayerMatchState& player = current.players()[winner.playerIndex];
  if (free.shot) {
    resolveShot(*free.shot, controlOutcome(current, *free.shot, there, claim, rules), context,
                next);
    free.shot.reset();
  }
  const HeaderStrike strike = executeHeader(
      intent, there, player,
      headerErrorFactor(timingSkill(player.attributes, winner.hands), jump.mistime, rules.aerial),
      rules.aerial, rules.physics,
      context.random(SimCore::RandomNumberGeneratorDomain::kExecution));
  BallState from = there;
  from.velocity = strike.velocity;
  from.verticalVelocity = strike.verticalVelocity;
  from.spin = 0.0;
  from.lastTouch = BallTouch{.playerId = player.playerId, .tick = context.tick()};
  next.setBallLastTouch(from.lastTouch);
  if (intent.play == AerialPlay::kShot) {
    const ShotRecord shot{
        .shooter = player.playerId, .from = there.position, .tick = context.tick()};
    next.setLastShot(shot);
    free.shot = shot;
    if (const auto& reception = current.lastReception();
        reception && reception->player == player.playerId) {
      next.setLastReception(std::nullopt);
    }
    const Goal goal = attackedGoal(current.pitch(), player.side);
    context.record(ShotAttempted{.tick = context.tick(),
                                 .shooter = player.playerId,
                                 .from = there.position,
                                 .target = intent.target,
                                 .height = intent.height,
                                 .struckAt = strike.target,
                                 .struckHeight = strike.height,
                                 .speed = strike.velocity.length(),
                                 .distance = (goal.center - there.position).length(),
                                 .opening = goalOpening(there.position, goal)});
  } else if (intent.receiver) {
    next.setLastPass(PassRecord{.passer = player.playerId,
                                .from = there.position,
                                .tick = context.tick(),
                                .receiver = intent.receiver});
    context.record(PassAttempted{.tick = context.tick(),
                                 .passer = player.playerId,
                                 .intendedReceiver = intent.receiver,
                                 .from = there.position,
                                 .target = intent.target,
                                 .speed = strike.velocity.length()});
  }
  flyOn(from, moment, free, rules, context, current, next);
}

// The high ball `first` got to first in this tick: everyone near enough goes
// up for it and AerialContest records them. Whoever wins it heads it, or, a
// keeper with his hands, holds it or punches it clear. Everyone who went up
// is in the air until he lands, and joins `excluded`, which leaves out of
// the contest who is in it. Returns whether anyone won it, which makes it
// the ball's tick; nobody reaching it, it flies on as if they were not there.
[[nodiscard]] bool contestInTheAir(const BallClaim& first, FreeBall& free, const BallRules& rules,
                                   const MatchStepContext& context, const MatchState& current,
                                   MatchStateWriter& next, std::vector<std::size_t>& excluded) {
  const double moment = first.contact.contactFraction * free.step.seconds;
  const BallState there = ballAfter(free.ball, rules.physics, moment);
  const std::vector<AerialChallenger> challengers =
      findChallengers(current, free.ball, free.step, first, rules.physics, context.tick(),
                      context.secondsPerTick(), rules.reception, rules.aerial, excluded);
  auto& random = context.random(SimCore::RandomNumberGeneratorDomain::kExecution);
  const AerialDuel duel = resolveAerialDuel(current, challengers, there.height, rules.aerial,
                                            rules.physics.gravity, random);
  AerialContest contest{.tick = context.tick(),
                        .position = there.position,
                        .height = there.height,
                        .contestants = {},
                        .winner = std::nullopt,
                        .play = std::nullopt};
  for (std::size_t index = 0; index < challengers.size(); ++index) {
    const std::size_t playerIndex = challengers[index].playerIndex;
    next.tactical(playerIndex).lastJump = context.tick();
    excluded.push_back(playerIndex);
    contest.contestants.push_back({.player = current.players()[playerIndex].playerId,
                                   .reach = duel.jumps[index].reach,
                                   .reached = duel.jumps[index].reached});
  }
  if (!duel.winner) {
    context.record(contest);
    return false;
  }
  const AerialChallenger& winner = challengers[*duel.winner];
  const PlayerMatchState& player = current.players()[winner.playerIndex];
  const BallClaim claim{
      .playerIndex = winner.playerIndex, .playerId = player.playerId, .contact = first.contact};
  contest.winner = player.playerId;

  HeaderIntent intent;
  if (winner.hands) {
    const bool contested =
        std::ranges::any_of(challengers, [&](const AerialChallenger& challenger) {
          return current.players()[challenger.playerIndex].side != player.side;
        });
    // Drawn always, held or not.
    if (random.nextUniform() < holdChance(player.attributes, contested, rules.aerial)) {
      contest.play = AerialPlay::kCaught;
      context.record(contest);
      takeBall(free, claim, player, rules, context, current, next);
      return true;
    }
    intent = clearanceIntent(current, winner.playerIndex, there, rules.aerial);
    intent.play = AerialPlay::kPunched;
  } else {
    intent = decideHeader(current, winner.playerIndex, there, rules.aerial,
                          context.random(SimCore::RandomNumberGeneratorDomain::kAi));
  }
  contest.play = intent.play;
  context.record(contest);
  playHeader(intent, claim, winner, duel.jumps[*duel.winner], there, moment, free, rules, context,
             current, next);
  return true;
}

// A free ball rolls, flies and bounces, and the first player to reach it on
// its way takes it. A shot still fast comes off an outfield player's body
// rather than to his feet, the keeper it faces meets it where it passes him,
// a high ball is contested in the air, and any ball comes off the goal
// frame; either way it flies on for the rest of the tick, and nothing else
// gets to it before the next. A high ball nobody won flies on past everyone
// who went up for it, to the keeper and to whoever else gets to it.
void moveFreeBall(const BallState& ball, const BallRules& rules, const MatchStepContext& context,
                  const MatchState& current, MatchStateWriter& next) {
  const Pitch& pitch = current.pitch();
  const double secondsPerTick = context.secondsPerTick();
  // The shot comes from the step's writer: one struck this step counts.
  FreeBall free = freeBallOf(ball, next.lastShot(), rules, pitch, secondsPerTick);

  const auto keeper = keeperFacing(free, rules, context, current, next);
  // The keeper facing the ball meets it his own way.
  std::vector<std::size_t> excluded;
  if (keeper) {
    excluded.push_back(keeper->index);
  }
  const bool deflects = free.shot && ball.velocity.length() >= rules.shooting.deflectionSpeed;
  const auto contactOf = [&] {
    return deflects ? findBallContact(current, ball, free.step, rules.physics, context.tick(),
                                      secondsPerTick, rules.reception,
                                      {.radius = rules.shooting.blockRadius,
                                       .height = rules.shooting.blockReach},
                                      excluded)
                    : findBallClaim(current, ball, free.step, rules.physics, context.tick(),
                                    secondsPerTick, rules.reception, excluded);
  };
  auto contact = contactOf();
  // A high ball someone gets to before anyone takes it at his feet is
  // contested in the air; a fast shot comes off a body instead.
  const auto aerial =
      deflects ? std::nullopt
               : findAerialContact(current, ball, free.step, rules.physics, context.tick(),
                                   secondsPerTick, rules.reception, rules.aerial, excluded);
  const bool inTheAir =
      aerial && (!contact || aerial->contact.contactFraction <= contact->contact.contactFraction);
  // The keeper meets the ball if it passes him before anyone else gets to
  // it, at most once: a ball that went past him flies on.
  std::optional<BallPassage> passage;
  const auto meetsKeeper = [&](const std::optional<BallClaim>& first) {
    if (!keeper || passage) {
      return false;
    }
    passage = findPlanePassage(ball, keeper->dive, rules.physics,
                               free.step.seconds * (first ? first->contact.contactFraction : 1.0));
    return passage && meetKeeper(*keeper, *passage, 0.0, free, rules, context, current, next);
  };
  if (meetsKeeper(inTheAir ? aerial : contact)) {
    return;
  }
  if (inTheAir) {
    if (contestInTheAir(*aerial, free, rules, context, current, next, excluded)) {
      return;
    }
    // Who went up for it does not take it at his feet as well.
    contact = contactOf();
    if (meetsKeeper(contact)) {
      return;
    }
  }
  if (contact && (!deflects || isGoalkeeper(current, contact->playerIndex))) {
    takeBall(free, *contact, current.players()[contact->playerIndex], rules, context, current,
             next);
    return;
  }

  BallState from = ball;
  if (contact) {
    const double moment = contact->contact.contactFraction * free.step.seconds;
    from = deflectedBy(*contact, free, rules, context, next);
    free.step = stepFreeBallTimed(from, rules.physics, pitch, secondsPerTick - moment);
  } else {
    from = offTheFrame(free, 0.0, rules, context, pitch);
  }
  settleFreeBall(from, free, rules, context, current, next);
}

// The ball's tick: the pending pass or shot, then the ball with its owner or
// on its own.
void moveBall(const BallRules& rules, const MatchStepContext& context, const MatchState& current,
              MatchStateWriter& next) {
  // 1. Play the pending pass or shot.
  const BallState ball = playPendingAction(rules, context, current, next);

  // 2. A controlled ball stays with its owner. create() and the writer
  //    accept no owner outside the state.
  if (ball.owner) {
    if (const auto index = findPlayerIndex(current, *ball.owner)) {
      carryBy(current.players()[*index], rules, context, current, next);
    }
    return;
  }

  // 3. A free ball moves on -- unless it is already out of play and the
  //    restart system, which runs after this one, is there to settle who
  //    plays on: a player standing on the line must not receive or intercept
  //    the ball first and have the restart overwrite him.
  if (rules.restarts.enabled && isOutOfPlay(ball, current.pitch())) {
    return;
  }
  moveFreeBall(ball, rules, context, current, next);
}

// Forgets a keeper's release of the ball from his hands once another player
// has touched it (hasReleasedBall()): from then on he may take it in them
// again.
void forgetReleases(const MatchState& current, MatchStateWriter& next) {
  const auto& touch = next.ball().lastTouch;
  for (std::size_t index = 0; index < current.players().size(); ++index) {
    PlayerTacticalState& tactical = next.tactical(index);
    if (tactical.handsReleased &&
        (!touch || touch->playerId != current.players()[index].playerId)) {
      tactical.handsReleased.reset();
    }
  }
}

}  // namespace

Vec2 carriedBallPosition(const PlayerMatchState& carrier, const BallPhysics& physics,
                         const Pitch& pitch) noexcept {
  return pitch.clamp(carrier.position + (carrier.facing * physics.carryDistance));
}

MatchSystem makeBallMovementSystem(const BallPhysics& physics, const PassConfig& passing,
                                   const ReceptionConfig& reception, const RestartConfig& restarts,
                                   const ShotConfig& shooting, const WoodworkConfig& woodwork,
                                   const ShotStoppingConfig& saves, const AerialConfig& aerial) {
  validate(physics);
  validate(passing);
  validate(reception);
  validate(shooting);
  validate(woodwork);
  validate(saves);
  validate(aerial);
  const BallRules rules{.physics = physics,
                        .passing = passing,
                        .reception = reception,
                        .restarts = restarts,
                        .shooting = shooting,
                        .woodwork = woodwork,
                        .saves = saves,
                        .aerial = aerial};
  return {.name = std::string(kBallMovementSystemName),
          .update = [rules](const MatchStepContext& context, const MatchState& current,
                            MatchStateWriter& next) {
            moveBall(rules, context, current, next);
            forgetReleases(current, next);
          }};
}

MatchSystem makeBallMovementSystem(const BallPhysics& physics, const PassConfig& passing,
                                   const ReceptionConfig& reception, const RestartConfig& restarts,
                                   const ShotConfig& shooting, const WoodworkConfig& woodwork,
                                   const ShotStoppingConfig& saves) {
  return makeBallMovementSystem(physics, passing, reception, restarts, shooting, woodwork, saves,
                                AerialConfig{});
}

MatchSystem makeBallMovementSystem(const BallPhysics& physics, const PassConfig& passing,
                                   const ReceptionConfig& reception, const RestartConfig& restarts,
                                   const ShotConfig& shooting, const WoodworkConfig& woodwork) {
  return makeBallMovementSystem(physics, passing, reception, restarts, shooting, woodwork,
                                ShotStoppingConfig{});
}

MatchSystem makeBallMovementSystem(const BallPhysics& physics, const PassConfig& passing,
                                   const ReceptionConfig& reception,
                                   const RestartConfig& restarts) {
  return makeBallMovementSystem(physics, passing, reception, restarts, ShotConfig{},
                                WoodworkConfig{});
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
