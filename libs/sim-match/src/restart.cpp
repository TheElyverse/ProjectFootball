#include "restart.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "ballMovement.hpp"
#include "matchEvents.hpp"
#include "passCandidates.hpp"
#include "teamFrame.hpp"
#include "vec2.hpp"
#include "zones.hpp"

namespace ElyverseFootball::SimMatch {
namespace {

[[nodiscard]] TeamSide otherSide(const TeamSide side) noexcept {
  return side == TeamSide::kHome ? TeamSide::kAway : TeamSide::kHome;
}

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
        takerOf(state, otherSide(goal->side), false, pitch.center(), formationPositions(state));
    return taker ? std::optional(RestartPlan{.kind = RestartKind::kKickoff, .playerIndex = *taker})
                 : std::nullopt;
  }
  const std::optional<TeamSide> touched = lastTouchSide(state);
  const bool overGoalLine = ball.position.x == 0.0 || ball.position.x == pitch.lengthMeters();
  if (!overGoalLine) {
    // Nobody touched it: the side whose half it lies in throws it in.
    const TeamSide halfOwner =
        ball.position.x < pitch.lengthMeters() / 2.0 ? TeamSide::kHome : TeamSide::kAway;
    const TeamSide thrower = touched ? otherSide(*touched) : halfOwner;
    const auto taker = takerOf(state, thrower, false, ball.position);
    return taker ? std::optional(RestartPlan{.kind = RestartKind::kThrowIn, .playerIndex = *taker})
                 : std::nullopt;
  }
  // Home defends x = 0, away x = length.
  const TeamSide defender = ball.position.x == 0.0 ? TeamSide::kHome : TeamSide::kAway;
  const bool corner = touched == defender;
  const TeamSide restarter = corner ? otherSide(defender) : defender;
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

MatchSystem makeRestartSystem(const RestartConfig& config, const BallPhysics& ball) {
  return {
      .name = std::string(kRestartSystemName),
      .update =
          [config, ball](const MatchStepContext& context, const MatchState& current,
                         MatchStateWriter& next) {
            if (!config.enabled) {
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
            context.record(PossessionChanged{
                .tick = context.tick(), .previousOwner = std::nullopt, .newOwner = taker.playerId});
            next.setBallOwner(taker.playerId);
            next.setBallLastTouch(BallTouch{.playerId = taker.playerId, .tick = context.tick()});
            next.setBallVelocity({});
            // A restart starts afresh: a keeper takes a goal kick with his feet.
            next.tactical(plan->playerIndex).handsReleased.reset();
            if (plan->kind == RestartKind::kGoalKick) {
              // The taker stands behind the ball in his goal area, facing up
              // the pitch.
              PlayerMatchState placed = taker;
              placed.facing = kickoffFacing(taker.side);
              placed.position =
                  current.pitch().clamp(goalKickSpot(current.pitch(), current.ball().position) -
                                        (placed.facing * ball.carryDistance));
              next.setPlayerPosition(plan->playerIndex, placed.position);
              next.setPlayerVelocity(plan->playerIndex, {});
              next.setPlayerTarget(plan->playerIndex, std::nullopt);
              next.setPlayerFacing(plan->playerIndex, placed.facing);
              next.setBallPosition(carriedBallPosition(placed, ball, current.pitch()));
              return;
            }
            const auto lineUp = plan->kind == RestartKind::kKickoff
                                    ? lineUpForKickoff(current, taker.side, ball)
                                    : std::nullopt;
            if (!lineUp) {
              next.setBallPosition(carriedBallPosition(taker, ball, current.pitch()));
              return;
            }
            // Both sides line up and start as at the beginning of a match.
            next.setBallPosition(current.pitch().center());
            for (std::size_t index = 0; index < current.players().size(); ++index) {
              next.setPlayerPosition(index, lineUp->positions[index]);
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
          },
      .intervalTicks = 1,
      .phaseTicks = 0};
}

}  // namespace ElyverseFootball::SimMatch
