#include "carrierScan.hpp"

#include <algorithm>
#include <array>
#include <cmath>

#include "carrying.hpp"
#include "responsibility.hpp"
#include "teamFrame.hpp"

namespace ElyverseFootball::SimMatch {
namespace {

using SimCore::Vec2;

// How far a point lies to the left of where a player faces: the cross
// product of his facing and the offset to it; positive to the left, negative
// to the right, zero straight ahead or behind.
[[nodiscard]] double leftOf(const PlayerMatchState& player, const Vec2 point) noexcept {
  const Vec2 offset = point - player.position;
  return (player.facing.x * offset.y) - (player.facing.y * offset.x);
}

}  // namespace

const CarrierScan* activeScan(const MatchState& state, const std::size_t playerIndex) {
  const auto& scan = state.tactical(playerIndex).scan;
  const BallState& ball = state.ball();
  if (!scan || ball.owner != state.players()[playerIndex].playerId) {
    return nullptr;
  }
  // His own touches carrying the ball on do not end it.
  if (ball.lastTouch && ball.lastTouch->tick > scan->since &&
      activeCarry(state, playerIndex) == nullptr) {
    return nullptr;
  }
  return &*scan;
}

Vec2 lookDirection(const MatchState& state, const std::size_t playerIndex) {
  if (const CarryRun* run = state.ball().touch ? activeCarry(state, playerIndex) : nullptr) {
    return run->gaze;
  }
  const CarrierScan* scan = activeScan(state, playerIndex);
  return scan != nullptr ? scan->gaze : state.players()[playerIndex].facing;
}

std::vector<LookTarget> lookTargets(const MatchState& state, const std::size_t carrierIndex) {
  const PlayerMatchState& carrier = state.players()[carrierIndex];
  const Pitch& pitch = state.pitch();
  const GoalEnd attacked = ownGoalEnd(opponentOf(carrier.side));
  const Vec2 goal{.x = pitch.goalLineX(attacked), .y = pitch.widthMeters() / 2.0};
  double turn = leftOf(carrier, goal);
  if (turn == 0.0) {
    turn = leftOf(carrier, pitch.center());
  }
  const bool firstLeft = turn >= 0.0;

  // The first side's teammates, then the other side's.
  std::array<Vec2, 2> sums{};
  std::array<int, 2> counts{};
  for (std::size_t index = 0; index < state.players().size(); ++index) {
    const PlayerMatchState& mate = state.players()[index];
    if (index == carrierIndex || mate.side != carrier.side) {
      continue;
    }
    const Vec2 spot = mate.target.value_or(mate.position);
    const double left = leftOf(carrier, spot);
    const bool first = left == 0.0 || (left > 0.0) == firstLeft;
    const std::size_t side = first ? 0 : 1;
    sums.at(side) = sums.at(side) + spot;
    ++counts.at(side);
  }

  const auto& tactic = state.tactics().of(carrier.side);
  const bool forward =
      tactic && tactic->responsibilityWeight(slotIndex(state, carrierIndex),
                                             SimTactics::Responsibility::kRunInBehind) > 0.0;
  std::vector<LookTarget> targets;
  if (forward) {
    targets.push_back({.point = pitch.penaltySpot(attacked), .firstSide = true});
  } else if (counts[0] > 0) {
    targets.push_back({.point = sums[0] * (1.0 / counts[0]), .firstSide = true});
  }
  if (counts[1] > 0) {
    targets.push_back({.point = sums[1] * (1.0 / counts[1]), .firstSide = false});
  }
  return targets;
}

Vec2 clearanceTarget(const MatchState& state, const std::size_t carrierIndex,
                     const ClearanceRange& range) {
  const double reach = range.reach;
  const PlayerMatchState& carrier = state.players()[carrierIndex];
  const Pitch& pitch = state.pitch();
  const Vec2 from = state.ball().position;
  const double width = pitch.widthMeters();
  // As far up as the kick reaches across to that touchline, short of the
  // goal line by its margin -- and never beyond his reach: a touchline or a
  // margin he cannot reach brings the point back toward him.
  const auto alongTouchline = [&](const double y) {
    const double across = std::abs(y - from.y);
    const double along = reach > across ? std::sqrt((reach * reach) - (across * across)) : 0.0;
    const double depth = std::min(depthOf(carrier.side, from, pitch) + along,
                                  pitch.lengthMeters() - range.goalLineMargin);
    const Vec2 point{.x = xAtDepth(carrier.side, depth, pitch), .y = y};
    const double distance = SimCore::distance(from, point);
    return pitch.clamp(distance > reach ? from + ((point - from) * (reach / distance)) : point);
  };
  const bool lowNearer = carrier.position.y <= width / 2.0;
  const double low = range.touchlineMargin;
  const double high = width - range.touchlineMargin;
  const Vec2 nearSide = alongTouchline(lowNearer ? low : high);
  const Vec2 farSide = alongTouchline(lowNearer ? high : low);
  // A side whose point lies too close to the ball -- he stands near that
  // touchline as far up as the margin lets him -- gives way to the other.
  const double nearDistance = SimCore::distance(from, nearSide);
  const double farDistance = SimCore::distance(from, farSide);
  const bool nearLong = nearDistance >= range.minDistance;
  const bool farLong = farDistance >= range.minDistance;
  if (nearLong != farLong || !nearLong) {
    return farDistance > nearDistance ? farSide : nearSide;
  }
  const auto& grid = state.pitchControl();
  if (grid && grid->controlAt(carrier.side, farSide) > grid->controlAt(carrier.side, nearSide)) {
    return farSide;
  }
  return nearSide;
}

}  // namespace ElyverseFootball::SimMatch
