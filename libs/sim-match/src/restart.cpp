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
    if (player.side != side || !isOnPitch(player)) {
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
    // A player sent off stays off the pitch.
    if (!isOnPitch(player)) {
      positions.push_back(player.position);
      continue;
    }
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
    case RestartKind::kIndirectFreeKick:
      return "indirectFreeKick";
    case RestartKind::kDirectFreeKick:
      return "directFreeKick";
    case RestartKind::kPenalty:
      return "penalty";
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
  const auto nearest = takerOf(state, kicking, false, circle.center, positions);
  if (!nearest) {
    return std::nullopt;
  }
  // The one the kickoff plan names, if any (docs/set-pieces.md#plans).
  const std::optional<std::size_t> taker =
      plannedTaker(state, kicking, SimTactics::SetPieceKind::kKickoff, *nearest);
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

void awardSetPiece(const MatchStepContext& context, const MatchState& current,
                   MatchStateWriter& next, const SetPieceAward& award,
                   const MatchClockConfig& clock) {
  using SimTactics::SetPieceKind;
  SetPieceKind kind = SetPieceKind::kThrowIn;
  if (award.kind == RestartKind::kCorner) {
    kind = SetPieceKind::kCorner;
  } else if (award.kind == RestartKind::kGoalKick) {
    kind = SetPieceKind::kGoalKick;
  } else if (award.kind == RestartKind::kIndirectFreeKick ||
             award.kind == RestartKind::kDirectFreeKick) {
    kind = SetPieceKind::kFreeKick;
  } else if (award.kind == RestartKind::kPenalty) {
    kind = SetPieceKind::kPenalty;
  }
  const std::size_t taker = plannedTaker(current, award.side, kind, award.fallback);
  const SimCore::PlayerId takerId = current.players()[taker].playerId;
  context.record(RestartTaken{
      .tick = context.tick(), .kind = award.kind, .player = takerId, .position = award.spot});
  if (const auto& owner = next.ball().owner) {
    context.record(PossessionChanged{
        .tick = context.tick(), .previousOwner = owner, .newOwner = std::nullopt});
    next.setBallHeld(false);
    next.setBallTouch(std::nullopt);
    next.setBallOwner(std::nullopt);
  }
  next.addLostSeconds(lostSeconds(award.kind, clock));
  next.setOffside(std::nullopt);
  next.setOffsideExemption(std::nullopt);
  next.setIndirectKick(std::nullopt);
  // Play has stopped: nobody plays on what he meant to, nor chases the ball.
  for (std::size_t index = 0; index < current.players().size(); ++index) {
    next.setPendingAction(index, std::nullopt);
  }
  for (const TeamSide side : {TeamSide::kHome, TeamSide::kAway}) {
    next.setChaser(side, std::nullopt);
  }
  next.setBallPosition(award.spot);
  next.setBallVelocity({});
  next.setBallHeight(0.0);
  next.setBallVerticalVelocity(0.0);
  next.setBallSpin(0.0);
  next.setBallSideSpin(0.0);
  next.tactical(taker).handsReleased.reset();
  next.tactical(taker).stumbledUntil.reset();
  next.setSetPiece(SetPiece{.kind = kind,
                            .side = award.side,
                            .taker = takerId,
                            .spot = award.spot,
                            .awarded = context.tick(),
                            .indirect = award.kind == RestartKind::kIndirectFreeKick});
}

bool awardFreeKick(const MatchStepContext& context, const MatchState& current,
                   MatchStateWriter& next, const FreeKick& kick, const MatchClockConfig& clock) {
  const auto taker = takerOf(current, kick.side, false, kick.spot);
  if (!taker) {
    return false;
  }
  // A shot still open was stopped by the whistle.
  if (const auto& shot = next.lastShot(); shot && !shot->resolved) {
    ShotRecord resolved = *shot;
    resolved.resolved = true;
    next.setLastShot(resolved);
    context.record(ShotResolved{.tick = context.tick(),
                                .shooter = shot->shooter,
                                .shotTick = shot->tick,
                                .outcome = ShotOutcome::kBlocked});
  }
  next.tactical(*taker).dive.reset();
  next.tactical(*taker).carry.reset();
  awardSetPiece(context, current, next,
                {.kind = kick.kind, .side = kick.side, .fallback = *taker, .spot = kick.spot},
                clock);
  return true;
}

void kickOff(const MatchStepContext& context, const MatchState& current, MatchStateWriter& next,
             const KickoffLineUp& lineUp, const bool asSetPiece) {
  const PlayerMatchState& taker = current.players()[lineUp.takerIndex];
  if (next.ball().owner != taker.playerId) {
    context.record(PossessionChanged{
        .tick = context.tick(), .previousOwner = next.ball().owner, .newOwner = taker.playerId});
  }
  next.setBallOwner(taker.playerId);
  next.setBallHeld(false);
  next.setBallTouch(std::nullopt);
  next.setBallLastTouch(BallTouch{.playerId = taker.playerId, .tick = context.tick()});
  next.setBallVelocity({});
  next.setBallHeight(0.0);
  next.setBallVerticalVelocity(0.0);
  next.setBallSpin(0.0);
  next.setBallSideSpin(0.0);
  takeKickoffLineUp(current, lineUp, next);
  if (asSetPiece) {
    next.setSetPiece(SetPiece{.kind = SimTactics::SetPieceKind::kKickoff,
                              .side = taker.side,
                              .taker = taker.playerId,
                              .spot = current.pitch().center(),
                              .awarded = context.tick()});
  }
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
  // Pitch control from where they stood before would outlive them, and so
  // would the play the offside rule watched.
  next.setPitchControl(std::nullopt);
  next.setOffside(std::nullopt);
  next.setOffsideExemption(std::nullopt);
  // A set piece being set up or an indirect free kick played ends with the
  // kickoff too.
  next.setIndirectKick(std::nullopt);
  next.setSetPiece(std::nullopt);
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
            // Nothing restarts while a set piece is set up.
            if (!config.enabled || current.setPiece() || next.setPiece()) {
              return;
            }
            const auto award = [&](const RestartKind kind, const TeamSide side,
                                   const std::size_t fallback, const SimCore::Vec2 spot) {
              awardSetPiece(context, current, next,
                            {.kind = kind, .side = side, .fallback = fallback, .spot = spot},
                            clock);
            };
            if (const auto corner = holdingCorner(current, context, config.maxHoldSeconds)) {
              const auto& [plan, spot] = *corner;
              award(plan.kind, current.players()[plan.playerIndex].side, plan.playerIndex, spot);
              return;
            }
            const auto plan = planRestart(current);
            if (!plan) {
              return;
            }
            const PlayerMatchState& taker = current.players()[plan->playerIndex];
            const Pitch& pitch = current.pitch();
            const SimCore::Vec2 out = current.ball().position;
            switch (plan->kind) {
              case RestartKind::kThrowIn:
                award(plan->kind, taker.side, plan->playerIndex, out);
                return;
              case RestartKind::kCorner:
                award(plan->kind, taker.side, plan->playerIndex,
                      {.x = out.x,
                       .y = out.y < pitch.widthMeters() / 2.0 ? 0.0 : pitch.widthMeters()});
                return;
              case RestartKind::kGoalKick:
                award(plan->kind, taker.side, plan->playerIndex, goalKickSpot(pitch, out));
                return;
              default:
                break;
            }
            // A kickoff: both sides line up at once, the taker on the ball.
            const auto lineUp = lineUpForKickoff(current, taker.side, ball);
            if (!lineUp) {
              return;
            }
            context.record(RestartTaken{.tick = context.tick(),
                                        .kind = RestartKind::kKickoff,
                                        .player = current.players()[lineUp->takerIndex].playerId,
                                        .position = pitch.center()});
            next.addLostSeconds(lostSeconds(RestartKind::kKickoff, clock));
            kickOff(context, current, next, *lineUp, true);
          },
      .intervalTicks = 1,
      .phaseTicks = 0};
}

}  // namespace ElyverseFootball::SimMatch
