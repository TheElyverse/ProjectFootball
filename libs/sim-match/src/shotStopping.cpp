#include "shotStopping.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>

#include "randomDraws.hpp"
#include "shooting.hpp"
#include "teamFrame.hpp"
#include "zones.hpp"

namespace ElyverseFootball::SimMatch {
namespace {

using RandomDraws::symmetricTriangular;
using RandomDraws::symmetricUniform;
using SimCore::Vec2;

[[nodiscard]] bool isFiniteNonNegative(const double value) noexcept {
  return value >= 0.0 && value <= std::numeric_limits<double>::max();
}

[[nodiscard]] bool isPositiveFinite(const double value) noexcept {
  return value > 0.0 && value <= std::numeric_limits<double>::max();
}

[[nodiscard]] bool isShare(const double value) noexcept {
  return value >= 0.0 && value <= 1.0;
}

// Across a plane square to this way along the ground: its left.
[[nodiscard]] Vec2 acrossOf(const Vec2 normal) noexcept {
  return {.x = -normal.y, .y = normal.x};
}

[[nodiscard]] double lengthOf(const PlanePoint point) noexcept {
  return std::sqrt((point.across * point.across) + (point.up * point.up));
}

[[nodiscard]] PlanePoint minus(const PlanePoint left, const PlanePoint right) noexcept {
  return {.across = left.across - right.across, .up = left.up - right.up};
}

// How far the ball's centre is from the line from his feet, `feet` across on
// the ground, to his hands.
[[nodiscard]] double distanceToBody(const double feet, const PlanePoint hands,
                                    const PlanePoint ball) noexcept {
  const PlanePoint body{.across = hands.across - feet, .up = hands.up};
  const PlanePoint offset{.across = ball.across - feet, .up = ball.up};
  const double lengthSquared = (body.across * body.across) + (body.up * body.up);
  const double along =
      lengthSquared > 0.0
          ? std::clamp(((offset.across * body.across) + (offset.up * body.up)) / lengthSquared, 0.0,
                       1.0)
          : 0.0;
  return lengthOf(minus(offset, {.across = body.across * along, .up = body.up * along}));
}

// Whether the ball's last touch is the shot's strike or a deflection of it.
[[nodiscard]] bool isShotFlight(const BallState& ball, const ShotRecord& shot) noexcept {
  return ball.lastTouch &&
         (*ball.lastTouch == BallTouch{.playerId = shot.shooter, .tick = shot.tick} ||
          ball.lastTouch == shot.deflection);
}

// How far beside or above the frame a ball crossing the goal line here
// misses it; 0 inside it.
[[nodiscard]] double missesFrameBy(const Goal& goal, const double pitchY,
                                   const double height) noexcept {
  return std::max({0.0, std::abs(pitchY - goal.center.y) - (goal.widthMeters / 2.0),
                   height - goal.heightMeters});
}

// Steps from not running at all to running all the way across to the point
// he read, among which he plans his run.
inline constexpr int kRunSteps = 16;

}  // namespace

void validate(const ShotStoppingConfig& config) {
  const bool valid =
      isFiniteNonNegative(config.slowestReaction) && isFiniteNonNegative(config.quickestReaction) &&
      config.quickestReaction <= config.slowestReaction && isFiniteNonNegative(config.readError) &&
      isFiniteNonNegative(config.readyHeight) && isPositiveFinite(config.diveReach) &&
      isPositiveFinite(config.jumpReach) && isPositiveFinite(config.diveSpeed) &&
      isPositiveFinite(config.bodyReach) && isPositiveFinite(config.catchableSpeed) &&
      isShare(config.minParrySpeed) && isShare(config.maxParrySpeed) &&
      config.minParrySpeed <= config.maxParrySpeed && isFiniteNonNegative(config.parryTilt) &&
      isFiniteNonNegative(config.parrySpread) && isFiniteNonNegative(config.carelessTryMargin) &&
      isFiniteNonNegative(config.carefulTryMargin) &&
      config.carefulTryMargin <= config.carelessTryMargin &&
      isFiniteNonNegative(config.wideMargin) && isFiniteNonNegative(config.diveBelowSpeed) &&
      isFiniteNonNegative(config.standingRecovery) && isFiniteNonNegative(config.stretchRecovery) &&
      config.standingRecovery <= config.stretchRecovery && isShare(config.reflexRecoveryShare);
  if (!valid) {
    throw std::invalid_argument("shot stopping: invalid configuration");
  }
}

double reactionSeconds(const double reflexes, const ShotStoppingConfig& config) noexcept {
  return config.slowestReaction - ((config.slowestReaction - config.quickestReaction) * reflexes);
}

double tryMargin(const double anticipation, const ShotStoppingConfig& config) noexcept {
  return config.carelessTryMargin -
         ((config.carelessTryMargin - config.carefulTryMargin) * anticipation);
}

double recoverySeconds(const double stretch, const PlayerAttributes& keeper,
                       const ShotStoppingConfig& config) noexcept {
  const double down =
      config.standingRecovery + ((config.stretchRecovery - config.standingRecovery) * stretch);
  return down * (1.0 - (config.reflexRecoveryShare * keeper.keeperReflexes));
}

std::optional<std::size_t> facingKeeper(const MatchState& state, const BallState& ball,
                                        const std::optional<ShotRecord>& shot,
                                        const BallPhysics& physics,
                                        const ShotStoppingConfig& config) {
  if (!shot || shot->resolved || ball.owner || !isShotFlight(ball, *shot)) {
    return std::nullopt;
  }
  if (!isInFlight(ball) && ball.velocity.length() < config.diveBelowSpeed) {
    return std::nullopt;
  }
  const auto shooter = findPlayerIndex(state, shot->shooter);
  if (!shooter) {
    return std::nullopt;
  }
  const TeamSide shooterSide = state.players()[*shooter].side;
  for (std::size_t index = 0; index < state.players().size(); ++index) {
    const PlayerMatchState& player = state.players()[index];
    if (player.side != shooterSide && isGoalkeeper(state, index)) {
      const bool onItsWay =
          predictGoalLineCrossing(ball, physics, state.pitch(), ownGoalEnd(player.side))
              .has_value();
      return onItsWay ? std::optional(index) : std::nullopt;
    }
  }
  return std::nullopt;
}

bool isBusy(const KeeperDive& dive, const double seconds) noexcept {
  return seconds < dive.landSeconds + dive.recoverySeconds;
}

bool isDiving(const MatchState& state, const std::size_t playerIndex, const SimCore::SimTick now,
              const double secondsPerTick) {
  const auto& dive = state.tactical(playerIndex).dive;
  return dive &&
         isBusy(*dive, static_cast<double>(now.value() - dive->tick.value()) * secondsPerTick);
}

PlanePoint readyHands(const ShotStoppingConfig& config) noexcept {
  return {.across = 0.0, .up = config.readyHeight};
}

PlanePoint clampToReach(const PlanePoint point, const ShotStoppingConfig& config) noexcept {
  const double across = point.across / config.diveReach;
  const double upward = point.up / config.jumpReach;
  const double reach = (across * across) + (upward * upward);
  if (reach <= 1.0) {
    return point;
  }
  const double scale = 1.0 / std::sqrt(reach);
  return {.across = point.across * scale, .up = point.up * scale};
}

double runDistance(const double seconds, const PlayerAttributes& attributes) noexcept {
  const double flatOut = attributes.maxSpeed / attributes.acceleration;
  if (seconds <= 0.0) {
    return 0.0;
  }
  if (seconds <= flatOut) {
    return attributes.acceleration * seconds * seconds / 2.0;
  }
  return (attributes.maxSpeed * flatOut / 2.0) + (attributes.maxSpeed * (seconds - flatOut));
}

double runSeconds(const double distance, const PlayerAttributes& attributes) noexcept {
  const double flatOut = attributes.maxSpeed / attributes.acceleration;
  const double speedingUp = attributes.maxSpeed * flatOut / 2.0;
  if (distance <= 0.0) {
    return 0.0;
  }
  if (distance <= speedingUp) {
    return std::sqrt(2.0 * distance / attributes.acceleration);
  }
  return flatOut + ((distance - speedingUp) / attributes.maxSpeed);
}

double feetAt(const KeeperDive& dive, const double seconds,
              const PlayerAttributes& attributes) noexcept {
  if (seconds >= dive.runSeconds) {
    return dive.feet;
  }
  const double run = std::min(std::abs(dive.feet), runDistance(seconds, attributes));
  return dive.feet < 0.0 ? -run : run;
}

PlanePoint handsAt(const KeeperDive& dive, const double seconds, const PlayerAttributes& attributes,
                   const ShotStoppingConfig& config) noexcept {
  const double feet = feetAt(dive, seconds, attributes);
  const PlanePoint ready = readyHands(config);
  const PlanePoint way = minus(dive.target, ready);
  const double length = lengthOf(way);
  const double diving = seconds - dive.runSeconds;
  const double share =
      diving > 0.0 && length > 0.0 ? std::min(1.0, config.diveSpeed * diving / length) : 0.0;
  return {.across = feet + ready.across + (way.across * share), .up = ready.up + (way.up * share)};
}

Vec2 bodyAt(const KeeperDive& dive, const double seconds, const PlayerAttributes& attributes,
            const ShotStoppingConfig& config) noexcept {
  return dive.origin + (acrossOf(dive.normal) * handsAt(dive, seconds, attributes, config).across);
}

std::optional<KeeperDive> decideDive(const MatchState& state, const std::size_t keeperIndex,
                                     const SimCore::SimTick now, const ShotStoppingConfig& config,
                                     const BallPhysics& physics,
                                     SimCore::RandomNumberGenerator& random) {
  const BallState& ball = state.ball();
  const auto standing = standingKeeper(state, keeperIndex, ball, now, config);
  if (!standing) {
    return std::nullopt;
  }
  // The plane is square to the ball's way, so the ball is this far from it.
  const double ahead = (standing->origin - ball.position).dot(standing->normal);
  const auto passage = ahead > 0.0 ? predictBallPassage(ball, physics, ahead) : std::nullopt;
  if (!passage) {
    return std::nullopt;
  }
  const PlayerMatchState& keeper = state.players()[keeperIndex];
  const PlayerAttributes& attributes = keeper.attributes;
  const double spread = config.readError * (1.0 - attributes.keeperAnticipation);
  const double acrossError = symmetricTriangular(random) * spread;
  const double upError = symmetricTriangular(random) * spread;
  const PlanePoint seen = planePoint(passage->ball, *standing);
  const PlanePoint read{.across = seen.across + acrossError,
                        .up = std::max(0.0, seen.up + upError)};

  // He leaves a ball he reads going clearly wide. Misread across his plane,
  // its way shifts by that much to its left, and so along the goal line by
  // that over how square to the line it comes.
  KeeperDive leave = *standing;
  leave.recoverySeconds = 0.0;
  const GoalEnd end = ownGoalEnd(keeper.side);
  if (const auto crossing = predictGoalLineCrossing(ball, physics, state.pitch(), end);
      crossing && standing->normal.x != 0.0 &&
      missesFrameBy(state.pitch().goal(end), crossing->y + (acrossError / standing->normal.x),
                    crossing->height + upError) > config.wideMargin) {
    return leave;
  }

  // He runs as far toward the point as time allows and dives the rest: of
  // every run, the one that gets his body closest to it by the time the ball
  // arrives, the longest of equally good ones.
  KeeperDive best = *standing;
  double bestGap = std::numeric_limits<double>::infinity();
  for (int step = 0; step <= kRunSteps; ++step) {
    KeeperDive plan = *standing;
    plan.feet = read.across * static_cast<double>(step) / kRunSteps;
    plan.target = clampToReach({.across = read.across - plan.feet, .up = read.up}, config);
    plan.runSeconds = runSeconds(std::abs(plan.feet), attributes);
    const double feet = feetAt(plan, passage->seconds, attributes);
    const PlanePoint hands = handsAt(plan, passage->seconds, attributes, config);
    const double gap = std::max(0.0, distanceToBody(feet, hands, read) - config.bodyReach);
    if (gap <= bestGap) {
      bestGap = gap;
      best = plan;
      best.landSeconds = passage->seconds;
      best.recoverySeconds = recoverySeconds(stretchOf(feet, hands, config), attributes, config);
    }
  }
  return bestGap > tryMargin(attributes.keeperAnticipation, config) ? leave : best;
}

std::optional<KeeperDive> standingKeeper(const MatchState& state, const std::size_t keeperIndex,
                                         const BallState& ball, const SimCore::SimTick now,
                                         const ShotStoppingConfig& config) {
  const double speed = ball.velocity.length();
  if (speed <= 0.0 || !ball.lastTouch) {
    return std::nullopt;
  }
  const PlayerMatchState& keeper = state.players()[keeperIndex];
  return KeeperDive{.touchedBy = ball.lastTouch->playerId,
                    .touchedAt = ball.lastTouch->tick,
                    .tick = now,
                    .origin = keeper.position,
                    .normal = ball.velocity * (1.0 / speed),
                    .feet = 0.0,
                    .target = readyHands(config),
                    .runSeconds = 0.0,
                    .landSeconds = 0.0,
                    .recoverySeconds = recoverySeconds(0.0, keeper.attributes, config)};
}

bool answers(const KeeperDive& dive, const BallState& ball) noexcept {
  return ball.lastTouch == BallTouch{.playerId = dive.touchedBy, .tick = dive.touchedAt};
}

std::optional<BallPassage> findPlanePassage(const BallState& ball, const KeeperDive& dive,
                                            const BallPhysics& physics,
                                            const double seconds) noexcept {
  const double speed = ball.velocity.length();
  const double toward = speed > 0.0 ? ball.velocity.dot(dive.normal) / speed : 0.0;
  const double ahead = (dive.origin - ball.position).dot(dive.normal);
  if (toward <= 0.0 || ahead <= 0.0) {
    return std::nullopt;
  }
  // Almost every tick the ball stays in front of the plane: nothing to search.
  if ((ballAfter(ball, physics, seconds).position - dive.origin).dot(dive.normal) < 0.0) {
    return std::nullopt;
  }
  auto passage = predictBallPassage(ball, physics, ahead / toward);
  if (passage && passage->seconds > seconds) {
    passage.reset();
  }
  return passage;
}

PlanePoint planePoint(const BallState& ball, const KeeperDive& dive) noexcept {
  return {.across = (ball.position - dive.origin).dot(acrossOf(dive.normal)),
          .up = ball.height + kBallRadius};
}

bool touchesKeeper(const double feet, const PlanePoint hands, const PlanePoint ball,
                   const ShotStoppingConfig& config) noexcept {
  return distanceToBody(feet, hands, ball) <= config.bodyReach;
}

double stretchOf(const double feet, const PlanePoint hands,
                 const ShotStoppingConfig& config) noexcept {
  const PlanePoint ready = readyHands(config);
  return std::min(1.0, lengthOf(minus(hands, {.across = feet + ready.across, .up = ready.up})) /
                           config.diveReach);
}

double catchChance(const BallState& ball, const double stretch, const PlayerAttributes& keeper,
                   const ShotStoppingConfig& config) noexcept {
  const double pace = 1.0 - std::min(1.0, ball.velocity.length() / config.catchableSpeed);
  const double ease = pace * (1.0 - stretch);
  return std::clamp(1.0 - ((1.0 - ease) * skillErrorFactor(keeper.keeperHandling)), 0.0, 1.0);
}

SaveTouch executeSave(const BallState& ball, const KeeperDive& dive, const double feet,
                      const PlanePoint hands, const PlayerAttributes& keeper,
                      const ShotStoppingConfig& config,
                      SimCore::RandomNumberGenerator& random) noexcept {
  // Drawn first and always, so the stream advances the same way for every
  // touch, held or not.
  const double hold = random.nextUniform();
  const double kept =
      config.minParrySpeed + (random.nextUniform() * (config.maxParrySpeed - config.minParrySpeed));
  const double acrossOff = symmetricUniform(random) * config.parrySpread;
  const double upOff = symmetricUniform(random) * config.parrySpread;

  SaveTouch touch{
      .caught = hold < catchChance(ball, stretchOf(feet, hands, config), keeper, config),
      .ball = ball};
  if (touch.caught) {
    return touch;
  }
  // His palm faces back the way the ball came, turned toward where he
  // stretched to: a ball he meets set goes back out, one at full stretch on
  // round the post or over the bar. Reaching down he still faces the ball, so
  // only a stretch upwards turns his palm.
  const PlanePoint ready = readyHands(config);
  const PlanePoint stretch = minus(hands, {.across = feet + ready.across, .up = ready.up});
  const double tiltAcross = (config.parryTilt * stretch.across / config.diveReach) + acrossOff;
  const double tiltUp = (config.parryTilt * std::max(0.0, stretch.up) / config.jumpReach) + upOff;
  const Vec2 palmPlane = (dive.normal * -1.0) + (acrossOf(dive.normal) * tiltAcross);
  const double palmLength = std::sqrt(palmPlane.lengthSquared() + (tiltUp * tiltUp));
  const Vec2 palm = palmPlane * (1.0 / palmLength);
  const double palmUp = tiltUp / palmLength;
  const double into = ball.velocity.dot(palm) + (ball.verticalVelocity * palmUp);
  if (into < 0.0) {
    const double push = -(1.0 + kept) * into;
    touch.ball.velocity = ball.velocity + (palm * push);
    touch.ball.verticalVelocity = ball.verticalVelocity + (palmUp * push);
  }
  const double speed = touch.ball.velocity.length();
  if (speed > kMaxBallSpeed) {
    touch.ball.velocity = touch.ball.velocity * (kMaxBallSpeed / speed);
  }
  touch.ball.verticalVelocity =
      std::clamp(touch.ball.verticalVelocity, -kMaxBallSpeed, kMaxBallSpeed);
  touch.ball.spin = 0.0;
  return touch;
}

MatchSystem makeShotStoppingSystem(const ShotStoppingConfig& config, const BallPhysics& physics) {
  validate(config);
  validate(physics);
  return {.name = std::string(kShotStoppingSystemName),
          .update = [config, physics](const MatchStepContext& context, const MatchState& current,
                                      MatchStateWriter& next) {
            const BallState& ball = current.ball();
            const auto keeper = facingKeeper(current, ball, current.lastShot(), physics, config);
            if (!keeper) {
              return;
            }
            const auto& answered = current.tactical(*keeper).dive;
            if ((answered && answers(*answered, ball)) ||
                isDiving(current, *keeper, context.tick(), context.secondsPerTick())) {
              return;
            }
            // A facing keeper faces a ball with a last touch.
            const auto touched = ball.lastTouch.value_or(BallTouch{});
            const double since =
                static_cast<double>(context.tick().value() - touched.tick.value()) *
                context.secondsPerTick();
            const PlayerAttributes& attributes = current.players()[*keeper].attributes;
            if (since < reactionSeconds(attributes.keeperReflexes, config)) {
              return;
            }
            if (const auto dive =
                    decideDive(current, *keeper, context.tick(), config, physics,
                               context.random(SimCore::RandomNumberGeneratorDomain::kAi))) {
              next.tactical(*keeper).dive = dive;
            }
          }};
}

}  // namespace ElyverseFootball::SimMatch
