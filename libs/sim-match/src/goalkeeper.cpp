#include "goalkeeper.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

#include "desiredRegion.hpp"
#include "pitch.hpp"
#include "randomDraws.hpp"
#include "tacticalPhases.hpp"
#include "teamFrame.hpp"

namespace ElyverseFootball::SimMatch {
namespace {

using RandomDraws::symmetricTriangular;
using SimCore::Vec2;

[[nodiscard]] bool isFiniteNonNegative(const double value) noexcept {
  return value >= 0.0 && value <= std::numeric_limits<double>::max();
}

[[nodiscard]] bool isFinite(const double value) noexcept {
  return std::abs(value) <= std::numeric_limits<double>::max();
}

[[nodiscard]] double lerp(const double from, const double toward, const double amount) noexcept {
  return from + (amount * (toward - from));
}

// The unit vector from the side's goal line into the pitch.
[[nodiscard]] Vec2 intoPitch(const TeamSide side, const Pitch& pitch) noexcept {
  return {.x = xAtDepth(side, 1.0, pitch) - xAtDepth(side, 0.0, pitch), .y = 0.0};
}

// The unit vector along offset, or the fallback -- the way into the pitch --
// if offset is zero or does not point into the pitch.
[[nodiscard]] Vec2 outward(const Vec2 offset, const Vec2 fallback) noexcept {
  const double length = offset.length();
  return length > 0.0 && offset.dot(fallback) > 0.0 ? offset * (1.0 / length) : fallback;
}

// Whether an opponent of the side has the ball.
[[nodiscard]] bool opponentOnTheBall(const MatchState& state, const TeamSide side) {
  const auto& owner = state.ball().owner;
  const auto index = owner ? findPlayerIndex(state, *owner) : std::nullopt;
  return index && state.players()[*index].side != side;
}

// The bisector of the angle the posts of the side's goal make as seen from
// the ball: where it meets the goal line and the way it runs into the pitch.
struct Bisector {
  Vec2 foot;
  Vec2 direction;
};

// The bisector meets the goal line where it divides the goal in the ratio of
// the ball's distances to the posts.
[[nodiscard]] Bisector goalBisector(const TeamSide side, const Vec2 ball, const Pitch& pitch) {
  const Goal goal = pitch.goal(ownGoalEnd(side));
  const Vec2 low = goal.postAtMinY();
  const Vec2 high = goal.postAtMaxY();
  const double toLow = (ball - low).length();
  const double toHigh = (ball - high).length();
  const double split = toLow + toHigh > 0.0 ? toLow / (toLow + toHigh) : 0.5;
  const Vec2 foot = low + ((high - low) * split);
  return {.foot = foot, .direction = outward(ball - foot, intoPitch(side, pitch))};
}

}  // namespace

void validate(const GoalkeeperConfig& config) {
  const bool valid =
      isFiniteNonNegative(config.lineDepth) && config.highDepthShare >= 0.0 &&
      config.highDepthShare <= 1.0 && config.possessionDepthShare >= 0.0 &&
      config.possessionDepthShare <= 1.0 && isFiniteNonNegative(config.positionErrorAlong) &&
      isFiniteNonNegative(config.positionErrorAcross) && isFinite(config.cautiousMargin) &&
      isFinite(config.boldMargin) && isFiniteNonNegative(config.sweepHysteresis) &&
      isFiniteNonNegative(config.misjudgement);
  if (!valid) {
    throw std::invalid_argument("goalkeeper: invalid configuration");
  }
}

Vec2 goalkeeperTarget(const MatchState& state, const std::size_t playerIndex,
                      const SimTactics::TacticalPhase phase, const GoalkeeperConfig& config,
                      const double shotRange) {
  const TeamSide side = state.players()[playerIndex].side;
  const Pitch& pitch = state.pitch();
  const Goal goal = pitch.goal(ownGoalEnd(side));
  const Vec2 ball = state.ball().position;
  const Vec2 low = goal.postAtMinY();
  const Vec2 high = goal.postAtMaxY();
  const auto [foot, direction] = goalBisector(side, ball, pitch);

  // High while the ball is far, on the line against a shot.
  const double line = defensiveLineDepth(state, side, phase);
  const PitchRect area = pitch.penaltyArea(ownGoalEnd(side));
  const double share = teamOnTheBall(state) == side
                           ? config.highDepthShare * config.possessionDepthShare
                           : config.highDepthShare;
  const double highDepth =
      std::max(config.lineDepth, std::min(share * line, area.max.x - area.min.x));
  const double fromGoal = (ball - goal.center).length();
  double depth =
      lerp(config.lineDepth, highDepth, line > 0.0 ? std::clamp(fromGoal / line, 0.0, 1.0) : 1.0);
  if (fromGoal <= shotRange && opponentOnTheBall(state, side)) {
    depth = config.lineDepth;
  }
  depth = std::min(depth, (ball - foot).length());

  // Never wider than his posts by more than he stands off his line: out wide
  // near the line he guards the near post rather than the corner flag.
  Vec2 target = foot + (direction * depth);
  const double offLine = depthOf(side, target, pitch);
  target.y = std::clamp(target.y, low.y - offLine, high.y + offLine);
  return pitch.clamp(target);
}

DesiredRegion goalkeeperRegion(const MatchState& state, const std::size_t playerIndex,
                               const SimTactics::TacticalPhase phase,
                               const GoalkeeperConfig& config, const double shotRange,
                               SimCore::RandomNumberGenerator& random) {
  const PlayerMatchState& keeper = state.players()[playerIndex];
  const Pitch& pitch = state.pitch();
  const Vec2 target = goalkeeperTarget(state, playerIndex, phase, config, shotRange);
  const Vec2 along = goalBisector(keeper.side, state.ball().position, pitch).direction;
  const Vec2 across{.x = -along.y, .y = along.x};
  const double spoil = 1.0 - keeper.attributes.keeperPositioning;
  const double alongError = symmetricTriangular(random) * config.positionErrorAlong * spoil;
  const double acrossError = symmetricTriangular(random) * config.positionErrorAcross * spoil;
  return {.tacticalTarget = target,
          .center = pitch.clamp(target + (along * alongError) + (across * acrossError)),
          .cost = {}};
}

double sweepThreshold(const double sweeping, const GoalkeeperConfig& config) noexcept {
  return lerp(config.cautiousMargin, config.boldMargin, sweeping);
}

double drawMisjudgement(const double anticipation, const GoalkeeperConfig& config,
                        SimCore::RandomNumberGenerator& random) noexcept {
  const double error = symmetricTriangular(random) * config.misjudgement * (1.0 - anticipation);
  // A perfect judge is off by zero, not by a negative zero.
  return error == 0.0 ? 0.0 : error;
}

SweepCall callSweep(const double keeperSeconds, const std::optional<double> attackerSeconds,
                    const double misjudgement, const double sweeping, const bool alreadyComing,
                    const GoalkeeperConfig& config) noexcept {
  SweepCall call{.keeperSeconds = keeperSeconds,
                 .attackerSeconds = attackerSeconds,
                 .misjudgement = misjudgement,
                 .threshold = sweepThreshold(sweeping, config) -
                              (alreadyComing ? config.sweepHysteresis : 0.0),
                 .coming = true};
  if (attackerSeconds) {
    call.coming = (*attackerSeconds - keeperSeconds) + misjudgement >= call.threshold;
  }
  return call;
}

}  // namespace ElyverseFootball::SimMatch
