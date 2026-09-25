#include "restart.hpp"

#include <cstddef>
#include <limits>
#include <optional>
#include <string>

#include "ballMovement.hpp"
#include "matchEvents.hpp"
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

// The side's player nearest to the ball, ties to the lower index; for a goal
// kick its goalkeeper first.
[[nodiscard]] std::optional<std::size_t> takerOf(const MatchState& state, const TeamSide side,
                                                 const bool goalkeeperFirst) {
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
    const double distance = SimCore::distance(player.position, state.ball().position);
    if (distance < nearestDistance) {
      nearest = index;
      nearestDistance = distance;
    }
  }
  return nearest;
}

}  // namespace

std::string_view restartKindName(const RestartKind kind) noexcept {
  switch (kind) {
    case RestartKind::kThrowIn:
      return "throwIn";
    case RestartKind::kGoalKick:
      return "goalKick";
    case RestartKind::kCorner:
      return "corner";
  }
  return "unknown";
}

bool isOutOfPlay(const MatchState& state) noexcept {
  const BallState& ball = state.ball();
  const Pitch& pitch = state.pitch();
  const bool onLine = ball.position.x == 0.0 || ball.position.x == pitch.lengthMeters() ||
                      ball.position.y == 0.0 || ball.position.y == pitch.widthMeters();
  return !ball.owner && ball.velocity == SimCore::Vec2{} && onLine && pitch.contains(ball.position);
}

std::optional<RestartPlan> planRestart(const MatchState& state) {
  if (!isOutOfPlay(state)) {
    return std::nullopt;
  }
  const BallState& ball = state.ball();
  const Pitch& pitch = state.pitch();
  const std::optional<TeamSide> touched = lastTouchSide(state);
  const bool overGoalLine = ball.position.x == 0.0 || ball.position.x == pitch.lengthMeters();
  if (!overGoalLine) {
    // Nobody touched it: the side whose half it lies in throws it in.
    const TeamSide halfOwner =
        ball.position.x < pitch.lengthMeters() / 2.0 ? TeamSide::kHome : TeamSide::kAway;
    const TeamSide thrower = touched ? otherSide(*touched) : halfOwner;
    const auto taker = takerOf(state, thrower, false);
    return taker ? std::optional(RestartPlan{.kind = RestartKind::kThrowIn, .playerIndex = *taker})
                 : std::nullopt;
  }
  // Home defends x = 0, away x = length.
  const TeamSide defender = ball.position.x == 0.0 ? TeamSide::kHome : TeamSide::kAway;
  const bool corner = touched == defender;
  const TeamSide restarter = corner ? otherSide(defender) : defender;
  const auto taker = takerOf(state, restarter, !corner);
  if (!taker) {
    return std::nullopt;
  }
  return RestartPlan{.kind = corner ? RestartKind::kCorner : RestartKind::kGoalKick,
                     .playerIndex = *taker};
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
                                        .position = current.ball().position});
            context.record(PossessionChanged{
                .tick = context.tick(), .previousOwner = std::nullopt, .newOwner = taker.playerId});
            next.setBallOwner(taker.playerId);
            next.setBallLastTouch(BallTouch{.playerId = taker.playerId, .tick = context.tick()});
            next.setBallVelocity({});
            next.setBallPosition(carriedBallPosition(taker, ball, current.pitch()));
          },
      .intervalTicks = 1,
      .phaseTicks = 0};
}

}  // namespace ElyverseFootball::SimMatch
