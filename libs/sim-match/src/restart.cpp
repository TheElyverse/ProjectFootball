#include "restart.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "ballPhysics.hpp"
#include "matchEvents.hpp"
#include "teamFrame.hpp"
#include "vec2.hpp"
#include "zones.hpp"

namespace ElyverseFootball::SimMatch {
namespace {

// The side of the player who touched the ball last, if anyone did.
[[nodiscard]] std::optional<TeamSide> lastTouchSide(const MatchState& state) {
  const auto& touch = state.ball().lastTouch;
  if (!touch) {
    return std::nullopt;
  }
  const auto index = findPlayerIndex(state, touch->playerId);
  if (!index) {
    return std::nullopt;
  }
  return state.players()[*index].side;
}

// The side's player nearest to a point, ties to the lower index; for a goal
// kick its goalkeeper first. lineUp, parallel to the players, says where they
// stand instead of their positions in the state, if it is not empty.
[[nodiscard]] std::optional<std::size_t> takerOf(const MatchState& state, const TeamSide side,
                                                 const bool goalkeeperFirst,
                                                 const SimCore::Vec2 point,
                                                 const std::span<const SimCore::Vec2> lineUp = {}) {
  std::optional<std::size_t> nearest;
  double nearestDistance = std::numeric_limits<double>::infinity();
  for (std::size_t index = 0; index < state.players().size(); ++index) {
    const PlayerMatchState& player = state.players()[index];
    if (player.side != side) {
      continue;
    }
    if (goalkeeperFirst && isGoalkeeper(state, index)) {
      return index;
    }
    const double distance =
        SimCore::distance(lineUp.empty() ? player.position : lineUp[index], point);
    if (distance < nearestDistance) {
      nearest = index;
      nearestDistance = distance;
    }
  }
  return nearest;
}

// Where every player stands in his side's starting formation, in his own
// half: his slot of the base shape at half its depth. A scripted side has no
// formation: its player stays where he is, moved straight back to the halfway
// line if he is beyond it. Parallel to the players.
[[nodiscard]] std::vector<SimCore::Vec2> formationPositions(const MatchState& state) {
  const Pitch& pitch = state.pitch();
  const double halfway = pitch.lengthMeters() / 2.0;
  std::vector<SimCore::Vec2> positions;
  positions.reserve(state.players().size());
  for (std::size_t index = 0; index < state.players().size(); ++index) {
    const PlayerMatchState& player = state.players()[index];
    const auto& tactic = state.tactics().of(player.side);
    if (!tactic) {
      const double depth = std::min(depthOf(player.side, player.position, pitch), halfway);
      positions.push_back(
          pitch.clamp({.x = xAtDepth(player.side, depth, pitch), .y = player.position.y}));
      continue;
    }
    const SimTactics::ShapePosition& shape = tactic->slots()[slotIndex(state, index)].position;
    positions.push_back({.x = xAtDepth(player.side, shape.depth * halfway, pitch),
                         .y = shape.width * pitch.widthMeters()});
  }
  return positions;
}

// A corner against a goalkeeper who has held the ball in his hands longer
// than maxHoldSeconds since his last touch, and the corner it is taken from:
// that of his goal line nearer the ball. A pass or shot he chose in time and
// the ball system plays in this step lets go of it in time.
[[nodiscard]] std::optional<std::pair<RestartPlan, SimCore::Vec2>> holdingCorner(
    const MatchState& state, const MatchStepContext& context, const double maxHoldSeconds) {
  const BallState& ball = state.ball();
  if (!ball.held || !ball.lastTouch || ball.lastTouch->playerId != ball.owner) {
    return std::nullopt;
  }
  const double held = context.time().secondsSince(ball.lastTouch->tick);
  const auto keeper = findPlayerIndex(state, ball.lastTouch->playerId);
  if (held <= maxHoldSeconds || !keeper || state.pendingAction(*keeper)) {
    return std::nullopt;
  }
  const Pitch& pitch = state.pitch();
  const TeamSide side = state.players()[*keeper].side;
  // Home defends x = 0, away x = length.
  const SimCore::Vec2 corner{
      .x = side == TeamSide::kHome ? 0.0 : pitch.lengthMeters(),
      .y = ball.position.y < pitch.widthMeters() / 2.0 ? 0.0 : pitch.widthMeters()};
  const auto taker = takerOf(state, opponentOf(side), false, corner);
  if (!taker) {
    return std::nullopt;
  }
  return std::pair{RestartPlan{.kind = RestartKind::kCorner, .playerIndex = *taker}, corner};
}

// Puts the taker on `position` at rest, facing `facing`, the ball at his
// feet.
void placeTaker(MatchStateWriter& next, const std::size_t index, PlayerMatchState taker,
                const SimCore::Vec2 position, const SimCore::Vec2 facing, const BallPhysics& ball,
                const Pitch& pitch) {
  taker.position = position;
  taker.facing = facing;
  next.setPlayerPosition(index, taker.position);
  next.setPlayerVelocity(index, {});
  next.setPlayerTarget(index, std::nullopt);
  next.setPlayerFacing(index, taker.facing);
  next.setBallPosition(carriedBallPosition(taker, ball, pitch));
}

}  // namespace

SimCore::Vec2 goalKickSpot(const Pitch& pitch, const SimCore::Vec2 out) noexcept {
  const GoalEnd end = out.x == 0.0 ? GoalEnd::kMinX : GoalEnd::kMaxX;
  const PitchRect area = pitch.goalArea(end);
  return {.x = end == GoalEnd::kMinX ? area.max.x : area.min.x,
          .y = std::clamp(out.y, area.min.y, area.max.y)};
}

std::string_view restartKindName(const RestartKind kind) noexcept {
  switch (kind) {
    case RestartKind::kThrowIn:
      return "throwIn";
    case RestartKind::kGoalKick:
      return "goalKick";
    case RestartKind::kCorner:
      return "corner";
    case RestartKind::kKickoff:
      return "kickoff";
    case RestartKind::kFreeKick:
      return "freeKick";
  }
  return "unknown";
}

bool isOutOfPlay(const BallState& ball, const Pitch& pitch) noexcept {
  const bool onLine = ball.position.x == 0.0 || ball.position.x == pitch.lengthMeters() ||
                      ball.position.y == 0.0 || ball.position.y == pitch.widthMeters();
  // At rest rather than merely without velocity in the pitch plane: a ball at
  // the apex of its flight has none either, and it is still in play.
  return !ball.owner && ball.isAtRest() && onLine && pitch.contains(ball.position);
}

bool isOutOfPlay(const MatchState& state) noexcept {
  return isOutOfPlay(state.ball(), state.pitch());
}

std::optional<RestartPlan> planRestart(const MatchState& state) {
  if (!isOutOfPlay(state)) {
    return std::nullopt;
  }
  const BallState& ball = state.ball();
  const Pitch& pitch = state.pitch();
  // A goal nobody has touched the ball since: the side that conceded it kicks
  // off.
  if (const auto& goal = state.lastGoal();
      goal && (!ball.lastTouch || goal->tick >= ball.lastTouch->tick)) {
    const auto taker =
        takerOf(state, opponentOf(goal->side), false, pitch.center(), formationPositions(state));
    return taker ? std::optional(RestartPlan{.kind = RestartKind::kKickoff, .playerIndex = *taker})
                 : std::nullopt;
  }
  const std::optional<TeamSide> touched = lastTouchSide(state);
  const bool overGoalLine = ball.position.x == 0.0 || ball.position.x == pitch.lengthMeters();
  if (!overGoalLine) {
    // Nobody touched it: the side whose half it lies in throws it in.
    const TeamSide halfOwner =
        ball.position.x < pitch.lengthMeters() / 2.0 ? TeamSide::kHome : TeamSide::kAway;
    const TeamSide thrower = touched ? opponentOf(*touched) : halfOwner;
    const auto taker = takerOf(state, thrower, false, ball.position);
    return taker ? std::optional(RestartPlan{.kind = RestartKind::kThrowIn, .playerIndex = *taker})
                 : std::nullopt;
  }
  // Home defends x = 0, away x = length.
  const TeamSide defender = ball.position.x == 0.0 ? TeamSide::kHome : TeamSide::kAway;
  const bool corner = touched == defender;
  const TeamSide restarter = corner ? opponentOf(defender) : defender;
  const auto taker = takerOf(state, restarter, !corner, ball.position);
  if (!taker) {
    return std::nullopt;
  }
  return RestartPlan{.kind = corner ? RestartKind::kCorner : RestartKind::kGoalKick,
                     .playerIndex = *taker};
}

std::optional<KickoffLineUp> lineUpForKickoff(const MatchState& state, const TeamSide kicking,
                                              const BallPhysics& ball) {
  const Pitch& pitch = state.pitch();
  const PitchCircle circle = pitch.centerCircle();
  std::vector<SimCore::Vec2> positions = formationPositions(state);
  const auto taker = takerOf(state, kicking, false, circle.center, positions);
  if (!taker) {
    return std::nullopt;
  }
  // The opponents keep out of the centre circle until the ball is played.
  for (std::size_t index = 0; index < positions.size(); ++index) {
    const TeamSide side = state.players()[index].side;
    SimCore::Vec2& position = positions[index];
    if (side == kicking || !circle.contains(position)) {
      continue;
    }
    // As a share of the radius, so that no square overflows on a huge pitch.
    const double across =
        std::clamp((position.y - circle.center.y) / circle.radiusMeters, -1.0, 1.0);
    const double back = circle.radiusMeters * std::sqrt((1.0 - across) * (1.0 + across));
    position.x = xAtDepth(side, depthOf(side, circle.center, pitch) - back, pitch);
  }
  // On a pitch too short for that, he stands on his goal line instead.
  positions[*taker] = pitch.clamp(circle.center - (ball.carryDistance * kickoffFacing(kicking)));
  return KickoffLineUp{.positions = std::move(positions), .takerIndex = *taker};
}

SimCore::Vec2 kickoffFacing(const TeamSide side) noexcept {
  return {.x = attackingDirection(side), .y = 0.0};
}

bool awardFreeKick(const MatchStepContext& context, const MatchState& current,
                   MatchStateWriter& next, const TeamSide side, const SimCore::Vec2 spot,
                   const BallPhysics& physics, const MatchClockConfig& clock) {
  const auto taker = takerOf(current, side, false, spot);
  if (!taker) {
    return false;
  }
  const PlayerMatchState& player = current.players()[*taker];
  context.record(RestartTaken{.tick = context.tick(),
                              .kind = RestartKind::kFreeKick,
                              .player = player.playerId,
                              .position = spot});
  if (next.ball().owner != player.playerId) {
    context.record(PossessionChanged{
        .tick = context.tick(), .previousOwner = next.ball().owner, .newOwner = player.playerId});
  }
  next.addLostSeconds(lostSeconds(RestartKind::kFreeKick, clock));
  // Play has stopped: nobody plays on what he meant to.
  for (std::size_t index = 0; index < current.players().size(); ++index) {
    next.setPendingAction(index, std::nullopt);
  }
  next.setBallOwner(player.playerId);
  next.setBallHeld(false);
  next.setBallTouch(std::nullopt);
  next.setBallLastTouch(BallTouch{.playerId = player.playerId, .tick = context.tick()});
  next.setBallVelocity({});
  next.setBallHeight(0.0);
  next.setBallVerticalVelocity(0.0);
  next.setBallSpin(0.0);
  next.setBallSideSpin(0.0);
  next.tactical(*taker).handsReleased.reset();
  next.tactical(*taker).stumbledUntil.reset();
  const SimCore::Vec2 facing = kickoffFacing(side);
  placeTaker(next, *taker, player, current.pitch().clamp(spot - (facing * physics.carryDistance)),
             facing, physics, current.pitch());
  return true;
}

void takeKickoffLineUp(const MatchState& current, const KickoffLineUp& lineUp,
                       MatchStateWriter& next) {
  next.setBallPosition(current.pitch().center());
  for (std::size_t index = 0; index < current.players().size(); ++index) {
    next.setPlayerPosition(index, lineUp.positions[index]);
    next.setPlayerVelocity(index, {});
    next.setPlayerTarget(index, std::nullopt);
    next.setPlayerFacing(index, kickoffFacing(current.players()[index].side));
    next.setPendingAction(index, std::nullopt);
    next.perception(index) = {};
    next.tactical(index) = {};
  }
  // Pitch control from where they stood before would outlive them.
  next.setPitchControl(std::nullopt);
  for (const TeamSide side : {TeamSide::kHome, TeamSide::kAway}) {
    next.setPress(side, std::nullopt);
    next.setChaser(side, std::nullopt);
  }
}

MatchSystem makeRestartSystem(const RestartRules& rules) {
  const RestartConfig& config = rules.restarts;
  const BallPhysics& ball = rules.physics;
  const MatchClockConfig& clock = rules.clock;
  // False for NaN.
  const bool valid =
      config.maxHoldSeconds >= 0.0 && config.maxHoldSeconds <= std::numeric_limits<double>::max();
  if (!valid) {
    throw std::invalid_argument("restart: invalid configuration");
  }
  return {
      .name = std::string(kRestartSystemName),
      .update =
          [config, ball, clock](const MatchStepContext& context, const MatchState& current,
                                MatchStateWriter& next) {
            if (!config.enabled) {
              return;
            }
            if (const auto corner = holdingCorner(current, context, config.maxHoldSeconds)) {
              const auto& [plan, spot] = *corner;
              const PlayerMatchState& taker = current.players()[plan.playerIndex];
              context.record(RestartTaken{.tick = context.tick(),
                                          .kind = plan.kind,
                                          .player = taker.playerId,
                                          .position = spot});
              next.addLostSeconds(lostSeconds(plan.kind, clock));
              next.setOffside(std::nullopt);
              next.setOffsideExemption(
                  BallTouch{.playerId = taker.playerId, .tick = context.tick()});
              context.record(PossessionChanged{.tick = context.tick(),
                                               .previousOwner = current.ball().owner,
                                               .newOwner = taker.playerId});
              if (const auto keeper = findPlayerIndex(current, *current.ball().owner)) {
                next.setPendingAction(*keeper, std::nullopt);
              }
              next.setBallOwner(taker.playerId);
              next.setBallLastTouch(BallTouch{.playerId = taker.playerId, .tick = context.tick()});
              next.setBallVelocity({});
              next.setBallHeight(0.0);
              next.setBallVerticalVelocity(0.0);
              next.tactical(plan.playerIndex).handsReleased.reset();
              next.tactical(plan.playerIndex).stumbledUntil.reset();
              placeTaker(
                  next, plan.playerIndex, taker, spot,
                  SimCore::unitOr(current.pitch().center() - spot, kickoffFacing(taker.side)), ball,
                  current.pitch());
              return;
            }
            const auto plan = planRestart(current);
            if (!plan) {
              return;
            }
            const PlayerMatchState& taker = current.players()[plan->playerIndex];
            context.record(RestartTaken{.tick = context.tick(),
                                        .kind = plan->kind,
                                        .player = taker.playerId,
                                        .position = plan->kind == RestartKind::kKickoff
                                                        ? current.pitch().center()
                                                        : current.ball().position});
            next.addLostSeconds(lostSeconds(plan->kind, clock));
            // No offside from a throw-in, a goal kick or a corner.
            next.setOffside(std::nullopt);
            next.setOffsideExemption(
                plan->kind == RestartKind::kKickoff
                    ? std::nullopt
                    : std::optional(BallTouch{.playerId = taker.playerId, .tick = context.tick()}));
            context.record(PossessionChanged{
                .tick = context.tick(), .previousOwner = std::nullopt, .newOwner = taker.playerId});
            next.setBallOwner(taker.playerId);
            next.setBallLastTouch(BallTouch{.playerId = taker.playerId, .tick = context.tick()});
            next.setBallVelocity({});
            // A restart starts afresh: a keeper takes a goal kick with his feet,
            // and a taker who stumbled in a duel is up again.
            next.tactical(plan->playerIndex).handsReleased.reset();
            next.tactical(plan->playerIndex).stumbledUntil.reset();
            if (plan->kind == RestartKind::kGoalKick) {
              // The taker stands behind the ball in his goal area, facing up
              // the pitch.
              const SimCore::Vec2 facing = kickoffFacing(taker.side);
              placeTaker(
                  next, plan->playerIndex, taker,
                  current.pitch().clamp(goalKickSpot(current.pitch(), current.ball().position) -
                                        (facing * ball.carryDistance)),
                  facing, ball, current.pitch());
              return;
            }
            const auto lineUp = plan->kind == RestartKind::kKickoff
                                    ? lineUpForKickoff(current, taker.side, ball)
                                    : std::nullopt;
            if (!lineUp) {
              next.setBallPosition(carriedBallPosition(taker, ball, current.pitch()));
              return;
            }
            takeKickoffLineUp(current, *lineUp, next);
          },
      .intervalTicks = 1,
      .phaseTicks = 0};
}

}  // namespace ElyverseFootball::SimMatch
