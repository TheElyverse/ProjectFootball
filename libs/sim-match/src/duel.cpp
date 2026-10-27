#include "duel.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>

#include "choicePolicy.hpp"
#include "matchEvents.hpp"
#include "pressingActions.hpp"
#include "randomDraws.hpp"
#include "shotCandidates.hpp"

namespace ElyverseFootball::SimMatch {
namespace {

using RandomDraws::symmetricTriangular;
using SimCore::unitOr;
using SimCore::Vec2;

[[nodiscard]] double clamp01(const double value) noexcept {
  return std::clamp(value, 0.0, 1.0);
}

// Where along the segment from `start` to `end` a point lies nearest, from 0
// at `start` to 1 at `end`; 0 for a segment without length.
[[nodiscard]] double segmentParameter(const Vec2 point, const Vec2 start, const Vec2 end) noexcept {
  const Vec2 along = end - start;
  const double lengthSquared = along.lengthSquared();
  if (lengthSquared == 0.0) {
    return 0.0;
  }
  return clamp01((point - start).dot(along) / lengthSquared);
}

[[nodiscard]] double distanceToSegment(const Vec2 point, const Vec2 start,
                                       const Vec2 end) noexcept {
  return SimCore::distance(point, start + ((end - start) * segmentParameter(point, start, end)));
}

// The unit vector from the carrier toward the goal he attacks; none on its
// centre.
[[nodiscard]] Vec2 towardGoal(const DuelGeometry& geometry) noexcept {
  return unitOr(geometry.goal - geometry.carrier, {});
}

// How squarely the defender stands in the carrier's way to goal: 1 straight
// goal-side of him, 0 level, -1 behind him.
[[nodiscard]] double goalSidedness(const DuelGeometry& geometry) noexcept {
  return unitOr(geometry.defender - geometry.carrier, {}).dot(towardGoal(geometry));
}

[[nodiscard]] double secondsSince(const SimCore::SimTick earlier, const SimCore::SimTick now,
                                  const double secondsPerTick) noexcept {
  return static_cast<double>(now.value() - earlier.value()) * secondsPerTick;
}

}  // namespace

void validate(const DuelConfig& config) {
  constexpr double kMax = std::numeric_limits<double>::max();
  const auto positive = [](const double value) { return value > 0.0 && value <= kMax; };
  const auto nonNegative = [](const double value) { return value >= 0.0 && value <= kMax; };
  const bool valid =
      config.intervalTicks >= 1 && nonNegative(config.protectSeconds) &&
      positive(config.engageRadius) && positive(config.breakRadius) &&
      config.engageRadius <= config.breakRadius && positive(config.jockeyDistance) &&
      positive(config.turnStep) && positive(config.accelerateStep) &&
      positive(config.tackleReach) && positive(config.footRadius) && positive(config.bodyRadius) &&
      nonNegative(config.footError) && config.minHold >= 0.0 && config.minHold <= config.maxHold &&
      config.maxHold <= 1.0 && nonNegative(config.looseSpeed) && positive(config.contactDistance) &&
      config.contactDistance < config.breakRadius && nonNegative(config.shovePerSecond) &&
      nonNegative(config.commitCost) && nonNegative(config.turnCost) &&
      nonNegative(config.recoveryPerSecond) && nonNegative(config.stumbleSeconds) &&
      nonNegative(config.shieldWeight) && nonNegative(config.turnWeight) &&
      nonNegative(config.accelerateWeight) && nonNegative(config.jockeyWeight) &&
      nonNegative(config.commitWeight) && positive(config.decisionTemperature);
  if (!valid) {
    throw std::invalid_argument("duel: invalid configuration");
  }
}

double wayClearance(const DuelGeometry& geometry, const DuelConfig& config) noexcept {
  const double passing = distanceToSegment(geometry.carrier, geometry.defender, geometry.ball);
  return clamp01((passing - config.bodyRadius) / config.bodyRadius);
}

double threat(const DuelGeometry& geometry, const double defenderBalance,
              const DuelConfig& config) noexcept {
  const double apart = SimCore::distance(geometry.carrier, geometry.defender);
  const double closeness =
      clamp01((config.breakRadius - apart) / (config.breakRadius - config.contactDistance));
  const double goalSide = (1.0 + goalSidedness(geometry)) / 2.0;
  return closeness * goalSide * defenderBalance;
}

std::array<double, 3> carrierUtilities(const DuelGeometry& geometry, const Duel& duel,
                                       const DuelConfig& config) noexcept {
  const double danger = threat(geometry, duel.defenderBalance, config);
  return {config.shieldWeight * danger, config.turnWeight * duel.carrierBalance,
          config.accelerateWeight * (1.0 - danger)};
}

std::vector<double> defenderUtilities(const DuelGeometry& geometry, const double defenderBalance,
                                      const DuelConfig& config) {
  std::vector<double> utilities{config.jockeyWeight};
  const double clearance = wayClearance(geometry, config);
  if (SimCore::distance(geometry.defender, geometry.ball) <= config.tackleReach &&
      clearance > 0.0) {
    utilities.push_back(config.commitWeight * clearance * defenderBalance);
  }
  return utilities;
}

Vec2 moveDirection(const DuelMove move, const DuelGeometry& geometry) noexcept {
  const Vec2 goal = towardGoal(geometry);
  const Vec2 away = unitOr(geometry.carrier - geometry.defender, goal * -1.0);
  Vec2 side{.x = -away.y, .y = away.x};
  if (side.dot(goal) < 0.0) {
    side = side * -1.0;
  }
  switch (move) {
    case DuelMove::kShield:
      return away;
    case DuelMove::kTurnAway:
      return side;
    case DuelMove::kAccelerate:
      return unitOr(goal + (side * std::max(0.0, goalSidedness(geometry))), side);
    case DuelMove::kJockey:
    case DuelMove::kCommit:
      return {};
  }
  return {};
}

Vec2 moveTarget(const DuelMove move, const DuelGeometry& geometry,
                const DuelConfig& config) noexcept {
  switch (move) {
    case DuelMove::kShield:
      return geometry.carrier;
    case DuelMove::kTurnAway:
      return geometry.carrier + (moveDirection(move, geometry) * config.turnStep);
    case DuelMove::kAccelerate:
      return geometry.carrier + (moveDirection(move, geometry) * config.accelerateStep);
    case DuelMove::kJockey:
      return pressTarget(geometry.carrier, std::nullopt, geometry.goal, config.jockeyDistance);
    case DuelMove::kCommit:
      return geometry.ball;
  }
  return geometry.carrier;
}

std::string_view lungeResultName(const LungeResult result) noexcept {
  switch (result) {
    case LungeResult::kMissed:
      return "missed";
    case LungeResult::kWon:
      return "won";
    case LungeResult::kLoose:
      return "loose";
    case LungeResult::kFoul:
      return "foul";
  }
  return "unknown";
}

LungeResult resolveLunge(const DuelGeometry& geometry, const Vec2 foot, const double carrierBalance,
                         const double ballControl, const DuelConfig& config) noexcept {
  const double offBall = SimCore::distance(foot, geometry.ball);
  const bool meetsBall = offBall <= config.footRadius;
  const bool meetsBody =
      distanceToSegment(geometry.carrier, geometry.defender, foot) < config.bodyRadius;
  const bool ballFirst =
      meetsBall && segmentParameter(geometry.ball, geometry.defender, foot) <=
                       segmentParameter(geometry.carrier, geometry.defender, foot);
  if (meetsBody && !ballFirst) {
    return LungeResult::kFoul;
  }
  if (!meetsBall) {
    return LungeResult::kMissed;
  }
  const double quality = 1.0 - (offBall / config.footRadius);
  const double hold =
      carrierBalance * (config.minHold + ((config.maxHold - config.minHold) * ballControl));
  if (quality > hold) {
    return LungeResult::kWon;
  }
  return quality > hold / 2.0 ? LungeResult::kLoose : LungeResult::kMissed;
}

namespace {

constexpr std::array kCarrierMoves{DuelMove::kShield, DuelMove::kTurnAway, DuelMove::kAccelerate};
constexpr auto kDecisions = SimCore::RandomNumberGeneratorDomain::kAi;

// What the duel system works with.
struct DuelRules {
  DuelConfig config;
  BallPhysics physics;
};

// Whether there is a ball at its owner's feet to fight for: not one rolling
// ahead of him after a touch, nor one a goalkeeper holds in his hands.
[[nodiscard]] bool canBeTakenOn(const BallState& ball) noexcept {
  return ball.isAtFeet() && !ball.held;
}

[[nodiscard]] DuelGeometry geometryOf(const MatchState& state, const std::size_t carrierIndex,
                                      const std::size_t defenderIndex) {
  const PlayerMatchState& carrier = state.players()[carrierIndex];
  return {.carrier = carrier.position,
          .defender = state.players()[defenderIndex].position,
          .ball = state.ball().position,
          .goal = attackedGoal(state.pitch(), carrier.side).center};
}

void stumble(const MatchStepContext& context, const std::size_t index, const DuelConfig& config,
             MatchStateWriter& next) {
  const auto ticks = static_cast<SimCore::SimTick::ValueType>(
      std::ceil(config.stumbleSeconds / context.secondsPerTick()));
  next.tactical(index).stumbledUntil = SimCore::SimTick(context.tick().value() + ticks);
}

void endDuel(const MatchStepContext& context, const MatchState& current,
             const std::size_t defenderIndex, const Duel& duel, const DuelOutcome outcome,
             MatchStateWriter& next) {
  next.tactical(defenderIndex).duel.reset();
  context.record(DuelEnded{.tick = context.tick(),
                           .carrier = duel.carrier,
                           .defender = current.players()[defenderIndex].playerId,
                           .outcome = outcome,
                           .position = current.ball().position});
}

// The defender takes the ball at the carrier's feet.
void winBall(const MatchStepContext& context, const MatchState& current,
             const std::size_t carrierIndex, const std::size_t defenderIndex,
             MatchStateWriter& next) {
  const SimCore::PlayerId winner = current.players()[defenderIndex].playerId;
  const SimCore::PlayerId loser = current.players()[carrierIndex].playerId;
  next.setBallOwner(winner);
  next.setBallLastTouch(BallTouch{.playerId = winner, .tick = context.tick()});
  next.setPendingAction(carrierIndex, std::nullopt);
  context.record(BallWon{.tick = context.tick(),
                         .winner = winner,
                         .loser = loser,
                         .position = current.ball().position,
                         .cause = BallWonCause::kDuel});
  context.record(
      PossessionChanged{.tick = context.tick(), .previousOwner = loser, .newOwner = winner});
}

// The carrier loses the ball at his feet: it moves on at this velocity, last
// touched by the player at index `touchedBy`, and he stumbles.
void loseBall(const MatchStepContext& context, const MatchState& current,
              const std::size_t carrierIndex, const std::size_t touchedBy, const Vec2 velocity,
              const DuelConfig& config, MatchStateWriter& next) {
  next.setBallOwner(std::nullopt);
  next.setBallVelocity(velocity);
  next.setBallLastTouch(
      BallTouch{.playerId = current.players()[touchedBy].playerId, .tick = context.tick()});
  next.setPendingAction(carrierIndex, std::nullopt);
  stumble(context, carrierIndex, config, next);
  context.record(PossessionChanged{.tick = context.tick(),
                                   .previousOwner = current.players()[carrierIndex].playerId,
                                   .newOwner = std::nullopt});
}

// The defender's foot knocks the ball away from it.
void loosenBall(const MatchStepContext& context, const MatchState& current,
                const std::size_t carrierIndex, const std::size_t defenderIndex, const Vec2 foot,
                const DuelConfig& config, MatchStateWriter& next) {
  const Vec2 ball = current.ball().position;
  const Vec2 lunge = unitOr(ball - current.players()[defenderIndex].position, {});
  loseBall(context, current, carrierIndex, defenderIndex,
           unitOr(ball - foot, lunge) * config.looseSpeed, config, next);
}

// The carrier, unless he is about to play the ball, decides his move: he
// rolls the ball round him that way, which costs balance by how far he turns
// it, and runs to the move's target -- or, turned off his feet by it, loses
// the ball where he rolled it, and the duel ends. The defender jockeys --
// once he is back on his feet. The duel is stored as it stands after both.
void moveOn(const MatchStepContext& context, const MatchState& current,
            const std::size_t carrierIndex, const std::size_t defenderIndex, Duel duel,
            const DuelRules& rules, MatchStateWriter& next) {
  const DuelConfig& config = rules.config;
  const DuelGeometry geometry = geometryOf(current, carrierIndex, defenderIndex);
  const Pitch& pitch = current.pitch();
  if (!next.pendingAction(carrierIndex)) {
    // A defender off his feet threatens nothing.
    Duel seen = duel;
    if (isStumbling(next.tactical(defenderIndex), context.tick())) {
      seen.defenderBalance = 0.0;
    }
    const auto utilities = carrierUtilities(geometry, seen, config);
    duel.carrierMove = kCarrierMoves.at(
        chooseByUtility(utilities, config.decisionTemperature, context.random(kDecisions))
            .value_or(0));
    const PlayerMatchState& carrier = current.players()[carrierIndex];
    const Vec2 direction = moveDirection(duel.carrierMove, geometry);
    duel.carrierBalance = std::max(
        0.0, duel.carrierBalance - (config.turnCost * (1.0 - carrier.facing.dot(direction)) / 2.0));
    next.setBallPosition(pitch.clamp(carrier.position + (direction * rules.physics.carryDistance)));
    if (duel.carrierBalance == 0.0) {
      endDuel(context, current, defenderIndex, duel, DuelOutcome::kLoose, next);
      loseBall(context, current, carrierIndex, carrierIndex, {}, config, next);
      return;
    }
    // Shielding, he stands rather than runs back to where he stood.
    next.setPlayerTarget(
        carrierIndex,
        duel.carrierMove == DuelMove::kShield
            ? std::nullopt
            : std::optional(pitch.clamp(moveTarget(duel.carrierMove, geometry, config))));
  }
  next.setPlayerTarget(defenderIndex, pitch.clamp(moveTarget(DuelMove::kJockey, geometry, config)));
  next.tactical(defenderIndex).duel = duel;
}

// Where the defender's lunge landed and what it did.
struct Lunge {
  Vec2 foot;
  LungeResult result = LungeResult::kMissed;
};

// The defender's lunge, decided and landed in this step.
[[nodiscard]] Lunge lunge(const MatchStepContext& context, const DuelGeometry& geometry,
                          const Duel& duel, const double ballControl, const DuelConfig& config) {
  auto& random = context.random(SimCore::RandomNumberGeneratorDomain::kExecution);
  const double spread = config.footError * (2.0 - duel.defenderBalance);
  const Vec2 aimed = geometry.ball + Vec2{.x = symmetricTriangular(random) * spread,
                                          .y = symmetricTriangular(random) * spread};
  // A lunge reaches no farther than tackleReach.
  const Vec2 reach = aimed - geometry.defender;
  const double length = reach.length();
  const Vec2 foot = length > config.tackleReach
                        ? geometry.defender + (reach * (config.tackleReach / length))
                        : aimed;
  return {.foot = foot,
          .result = resolveLunge(geometry, foot, duel.carrierBalance, ballControl, config)};
}

// One step of the duel the defender at this index fights.
void stepDuel(const MatchStepContext& context, const MatchState& current,
              const std::size_t defenderIndex, const Duel& duel, const DuelRules& rules,
              MatchStateWriter& next) {
  const DuelConfig& config = rules.config;
  const BallState& ball = current.ball();
  const PlayerMatchState& defender = current.players()[defenderIndex];
  const auto carrierIndex = findPlayerIndex(current, duel.carrier);
  const auto& action = current.tactical(defenderIndex).action;
  const bool onTheBall = carrierIndex && ball.owner == duel.carrier && canBeTakenOn(ball);
  const bool pressing = action && action->type == ActionType::kPressCarrier;
  const double apart =
      carrierIndex ? SimCore::distance(defender.position, current.players()[*carrierIndex].position)
                   : 0.0;
  if (!onTheBall || !pressing || apart > config.breakRadius) {
    endDuel(context, current, defenderIndex, duel, DuelOutcome::kKept, next);
    return;
  }
  const PlayerMatchState& carrier = current.players()[*carrierIndex];
  const double seconds = config.intervalTicks * context.secondsPerTick();
  const bool contact = apart < config.contactDistance;
  // A defender off his feet pushes nobody and wins nothing.
  const bool defenderDown = isStumbling(current.tactical(defenderIndex), context.tick());
  // Both regain balance, and in contact each is pushed off it the more, the
  // stronger the other is.
  const auto balanceAfter = [&](const double balance, const PlayerAttributes& own,
                                const PlayerAttributes& other, const bool pushed) {
    const double push =
        pushed ? config.shovePerSecond * clamp01(0.5 + other.strength - own.strength) : 0.0;
    return clamp01(balance + ((config.recoveryPerSecond - push) * seconds));
  };
  Duel stepped = duel;
  stepped.carrierBalance = balanceAfter(duel.carrierBalance, carrier.attributes,
                                        defender.attributes, contact && !defenderDown);
  stepped.defenderBalance =
      balanceAfter(duel.defenderBalance, defender.attributes, carrier.attributes, contact);
  // Off his feet -- only a push can take him there while he regains balance
  // -- he loses the ball to the defender against him.
  if (stepped.carrierBalance == 0.0) {
    endDuel(context, current, defenderIndex, duel, DuelOutcome::kWon, next);
    winBall(context, current, *carrierIndex, defenderIndex, next);
    stumble(context, *carrierIndex, config, next);
    return;
  }
  stepped.defenderMove = DuelMove::kJockey;
  if (!defenderDown) {
    const DuelGeometry geometry = geometryOf(current, *carrierIndex, defenderIndex);
    const std::vector<double> utilities =
        defenderUtilities(geometry, stepped.defenderBalance, config);
    if (chooseByUtility(utilities, config.decisionTemperature, context.random(kDecisions)) == 1) {
      stepped.defenderMove = DuelMove::kCommit;
      const Lunge landed =
          lunge(context, geometry, stepped, carrier.attributes.ballControl, config);
      stepped.defenderBalance = std::max(0.0, stepped.defenderBalance - config.commitCost);
      switch (landed.result) {
        case LungeResult::kWon:
          endDuel(context, current, defenderIndex, duel, DuelOutcome::kWon, next);
          winBall(context, current, *carrierIndex, defenderIndex, next);
          return;
        case LungeResult::kLoose:
          endDuel(context, current, defenderIndex, duel, DuelOutcome::kLoose, next);
          loosenBall(context, current, *carrierIndex, defenderIndex, landed.foot, config, next);
          return;
        case LungeResult::kFoul:
          endDuel(context, current, defenderIndex, duel, DuelOutcome::kFoul, next);
          stumble(context, defenderIndex, config, next);
          return;
        case LungeResult::kMissed:
          stumble(context, defenderIndex, config, next);
          break;
      }
    }
  }
  moveOn(context, current, *carrierIndex, defenderIndex, stepped, rules, next);
}

// The presser who takes on the carrier at this index, if any: the nearest of
// the other side who presses him, stands within engageRadius and is on his
// feet; ties to the first in player order.
[[nodiscard]] std::optional<std::size_t> findDefender(const MatchStepContext& context,
                                                      const MatchState& current,
                                                      const std::size_t carrierIndex,
                                                      const DuelConfig& config) {
  const PlayerMatchState& carrier = current.players()[carrierIndex];
  std::optional<std::size_t> nearest;
  double nearestDistance = 0.0;
  for (std::size_t index = 0; index < current.players().size(); ++index) {
    const PlayerMatchState& player = current.players()[index];
    const PlayerTacticalState& tactical = current.tactical(index);
    const bool pressing = player.side != carrier.side && tactical.action &&
                          tactical.action->type == ActionType::kPressCarrier;
    const double distance = SimCore::distance(player.position, carrier.position);
    if (pressing && !isStumbling(tactical, context.tick()) && distance <= config.engageRadius &&
        (!nearest || distance < nearestDistance)) {
      nearest = index;
      nearestDistance = distance;
    }
  }
  return nearest;
}

// Starts a duel on a carrier who has had the ball at his feet long enough, if
// a presser is there to take him on.
void startDuel(const MatchStepContext& context, const MatchState& current, const DuelRules& rules,
               MatchStateWriter& next) {
  const DuelConfig& config = rules.config;
  const BallState& ball = current.ball();
  if (!ball.owner || !canBeTakenOn(ball)) {
    return;
  }
  const SimCore::PlayerId owner = *ball.owner;
  const auto carrierIndex = findPlayerIndex(current, owner);
  const bool settled =
      !ball.lastTouch || secondsSince(ball.lastTouch->tick, context.tick(),
                                      context.secondsPerTick()) >= config.protectSeconds;
  if (!carrierIndex || !settled) {
    return;
  }
  const auto defenderIndex = findDefender(context, current, *carrierIndex, config);
  if (!defenderIndex) {
    return;
  }
  context.record(DuelStarted{.tick = context.tick(),
                             .carrier = owner,
                             .defender = current.players()[*defenderIndex].playerId,
                             .position = ball.position});
  moveOn(context, current, *carrierIndex, *defenderIndex,
         Duel{.carrier = owner, .since = context.tick()}, rules, next);
}

}  // namespace

MatchSystem makeDuelSystem(const DuelConfig& config, const BallPhysics& physics) {
  validate(config);
  validate(physics);
  return {
      .name = std::string(kDuelSystemName),
      .update =
          [rules = DuelRules{.config = config, .physics = physics}](
              const MatchStepContext& context, const MatchState& current, MatchStateWriter& next) {
            for (std::size_t index = 0; index < current.players().size(); ++index) {
              if (const auto& duel = current.tactical(index).duel) {
                stepDuel(context, current, index, *duel, rules, next);
                return;
              }
            }
            startDuel(context, current, rules, next);
          },
      .intervalTicks = config.intervalTicks,
      .phaseTicks = 0};
}

MatchSystem makeDuelSystem(const DuelConfig& config) {
  return makeDuelSystem(config, BallPhysics{});
}

}  // namespace ElyverseFootball::SimMatch
