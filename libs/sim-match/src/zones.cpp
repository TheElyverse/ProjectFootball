#include "zones.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <utility>
#include <vector>

#include "responsibility.hpp"
#include "teamFrame.hpp"

namespace ElyverseFootball::SimMatch {
namespace {

// A side's distance from its left touchline, looking toward the opponent's
// goal: home attacks +x, so its left is y = width; away's left is y = 0.
[[nodiscard]] double fromLeft(const TeamSide side, const double pitchY,
                              const Pitch& pitch) noexcept {
  return side == TeamSide::kHome ? pitch.widthMeters() - pitchY : pitchY;
}

// The pitch y band of the distances [near, far] from a side's left touchline.
[[nodiscard]] std::pair<double, double> yBand(const TeamSide side, const double near,
                                              const double far, const Pitch& pitch) noexcept {
  if (side == TeamSide::kHome) {
    return {pitch.widthMeters() - far, pitch.widthMeters() - near};
  }
  return {near, far};
}

// The pitch x band between two depths of a side, in either order.
[[nodiscard]] std::pair<double, double> xBand(const TeamSide side, const double firstDepth,
                                              const double secondDepth,
                                              const Pitch& pitch) noexcept {
  const double first = xAtDepth(side, firstDepth, pitch);
  const double second = xAtDepth(side, secondDepth, pitch);
  return {std::min(first, second), std::max(first, second)};
}

[[nodiscard]] PitchRect rectOf(const std::pair<double, double> xBounds,
                               const std::pair<double, double> yBounds) noexcept {
  return {.min = {.x = xBounds.first, .y = yBounds.first},
          .max = {.x = xBounds.second, .y = yBounds.second}};
}

}  // namespace

std::string_view laneName(const Lane lane) noexcept {
  switch (lane) {
    case Lane::kLeftWing:
      return "leftWing";
    case Lane::kLeftHalfspace:
      return "leftHalfspace";
    case Lane::kCentre:
      return "centre";
    case Lane::kRightHalfspace:
      return "rightHalfspace";
    case Lane::kRightWing:
      return "rightWing";
  }
  return "unknown";
}

Lane laneOf(const TeamSide side, const SimCore::Vec2 position, const Pitch& pitch) noexcept {
  const double fraction =
      std::clamp(fromLeft(side, position.y, pitch) / pitch.widthMeters(), 0.0, 1.0);
  // Multiplying by the reciprocal of kLaneFraction lands exactly on an exact
  // fifth (e.g. 0.6 * 5.0 == 3.0), whereas dividing by kLaneFraction itself
  // (0.6 / 0.2) rounds to just under it, misclassifying that boundary.
  constexpr double kLanesPerWidth = 1.0 / kLaneFraction;
  const auto index = std::min(4.0, std::floor(fraction * kLanesPerWidth));
  return static_cast<Lane>(static_cast<std::uint8_t>(index));
}

PitchRect laneRect(const TeamSide side, const Lane lane, const Pitch& pitch) noexcept {
  const double near = static_cast<double>(lane) * kLaneFraction * pitch.widthMeters();
  const double far = near + (kLaneFraction * pitch.widthMeters());
  return rectOf({0.0, pitch.lengthMeters()}, yBand(side, near, far, pitch));
}

double laneCenterY(const TeamSide side, const Lane lane, const Pitch& pitch) noexcept {
  const PitchRect rect = laneRect(side, lane, pitch);
  return (rect.min.y + rect.max.y) / 2.0;
}

std::string_view thirdName(const Third third) noexcept {
  switch (third) {
    case Third::kDefensive:
      return "defensive";
    case Third::kMiddle:
      return "middle";
    case Third::kAttacking:
      return "attacking";
  }
  return "unknown";
}

Third thirdOf(const TeamSide side, const SimCore::Vec2 position, const Pitch& pitch) noexcept {
  // Compare against pitch-x boundaries built the same way thirdRect() builds
  // them (via xAtDepth), rather than against a depth recomputed from
  // position.x, so the two agree exactly at a third's boundary.
  const double length = pitch.lengthMeters() / 3.0;
  const double firstBoundary = xAtDepth(side, length, pitch);
  const double secondBoundary = xAtDepth(side, 2.0 * length, pitch);
  if (side == TeamSide::kHome) {
    if (position.x < firstBoundary) {
      return Third::kDefensive;
    }
    return position.x < secondBoundary ? Third::kMiddle : Third::kAttacking;
  }
  if (position.x > firstBoundary) {
    return Third::kDefensive;
  }
  return position.x > secondBoundary ? Third::kMiddle : Third::kAttacking;
}

PitchRect thirdRect(const TeamSide side, const Third third, const Pitch& pitch) noexcept {
  const double length = pitch.lengthMeters() / 3.0;
  const double near = static_cast<double>(third) * length;
  return rectOf(xBand(side, near, near + length, pitch), {0.0, pitch.widthMeters()});
}

bool isGoalkeeper(const MatchState& state, const std::size_t playerIndex) {
  const auto& tactic = state.tactics().of(state.players()[playerIndex].side);
  return tactic && tactic->responsibilityWeight(slotIndex(state, playerIndex),
                                                SimTactics::Responsibility::kGuardGoal) > 0.0;
}

std::optional<TeamShape> measureTeamShape(const MatchState& state, const TeamSide side) {
  std::vector<double> depths;
  TeamShape shape;
  shape.minY = std::numeric_limits<double>::infinity();
  shape.maxY = -std::numeric_limits<double>::infinity();
  SimCore::Vec2 sum;
  for (std::size_t index = 0; index < state.players().size(); ++index) {
    const PlayerMatchState& player = state.players()[index];
    if (player.side != side || isGoalkeeper(state, index)) {
      continue;
    }
    depths.push_back(depthOf(side, player.position, state.pitch()));
    shape.minY = std::min(shape.minY, player.position.y);
    shape.maxY = std::max(shape.maxY, player.position.y);
    sum = sum + player.position;
  }
  if (depths.empty()) {
    return std::nullopt;
  }
  std::ranges::sort(depths);
  const std::size_t count = depths.size();
  shape.defensiveLine = depths.front();
  shape.frontLine = depths.back();
  shape.midfieldLine = count % 2 == 1 ? depths.at(count / 2)
                                      : (depths.at((count / 2) - 1) + depths.at(count / 2)) / 2.0;
  shape.length = shape.frontLine - shape.defensiveLine;
  shape.width = shape.maxY - shape.minY;
  shape.centroid = sum * (1.0 / static_cast<double>(count));
  return shape;
}

PitchRect betweenLines(const TeamSide opponent, const TeamShape& opponentShape,
                       const Pitch& pitch) noexcept {
  return rectOf(xBand(opponent, opponentShape.defensiveLine, opponentShape.midfieldLine, pitch),
                {opponentShape.minY, opponentShape.maxY});
}

PitchRect behindLine(const TeamSide opponent, const TeamShape& opponentShape,
                     const Pitch& pitch) noexcept {
  return rectOf(xBand(opponent, 0.0, opponentShape.defensiveLine, pitch),
                {0.0, pitch.widthMeters()});
}

PitchRect restDefenceZone(const TeamSide side, const SimCore::Vec2 ballPosition,
                          const Pitch& pitch) noexcept {
  const double ballDepth = depthOf(side, ballPosition, pitch);
  const double near = std::max(0.0, ballDepth - kRestDefenceNear);
  const double far = std::max(0.0, ballDepth - kRestDefenceFar);
  const double width = pitch.widthMeters();
  return rectOf(xBand(side, far, near, pitch),
                {kLaneFraction * width, (1.0 - kLaneFraction) * width});
}

}  // namespace ElyverseFootball::SimMatch
