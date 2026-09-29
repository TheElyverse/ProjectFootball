#include "shotCandidates.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <optional>
#include <stdexcept>
#include <tuple>
#include <utility>

#include "laneReach.hpp"
#include "passCandidates.hpp"
#include "pitch.hpp"
#include "spatialQueries.hpp"
#include "zones.hpp"

namespace ElyverseFootball::SimMatch {
namespace {

using LaneReach::Lane;
using LaneReach::Remembered;
using SimCore::Vec2;

// How much wider than an average finisher's a shooter's spread is: a
// pinpoint one (accuracy 1) spreads a tenth as much, an average one (0.5)
// exactly as much, a wild one (0) almost twice.
[[nodiscard]] double accuracyFactor(const double accuracy) noexcept {
  constexpr double kPinpoint = 0.1;
  return kPinpoint + (2.0 * (1.0 - kPinpoint) * (1.0 - accuracy));
}

// The share of a triangular spread of half-width `spread` around `aim` that
// falls below `value`: the distribution function of the triangle, built from
// arithmetic only.
[[nodiscard]] double spreadBelow(const double value, const double aim,
                                 const double spread) noexcept {
  const double offset = (value - aim) / spread;
  if (offset <= -1.0) {
    return 0.0;
  }
  if (offset >= 1.0) {
    return 1.0;
  }
  if (offset <= 0.0) {
    return (offset + 1.0) * (offset + 1.0) / 2.0;
  }
  return 1.0 - ((1.0 - offset) * (1.0 - offset) / 2.0);
}

// How wide the goal looks from a point: the distance between the directions
// to its two posts as unit vectors, 2·sin(angle / 2). No trigonometry, so no
// platform differences.
[[nodiscard]] double openingOf(const Vec2 from, const Goal& goal) noexcept {
  const Vec2 toMin = goal.postAtMinY() - from;
  const Vec2 toMax = goal.postAtMaxY() - from;
  const double minLength = toMin.length();
  const double maxLength = toMax.length();
  if (minLength <= 0.0 || maxLength <= 0.0) {
    return 0.0;
  }
  return ((toMin * (1.0 / minLength)) - (toMax * (1.0 / maxLength))).length();
}

// What becomes of a shot at one zone: the chance it is blocked on its way,
// and, if it gets there, that it is saved or goes in.
struct ZoneOutcome {
  double block = 0.0;
  double save = 0.0;
  double goal = 0.0;
};

// The share of a shot's spread that one zone of a row or column takes: the
// part that ends up inside it, and the part that passes it on its way -- the
// zone on the frame also passes every shot that misses beyond it, so a
// blocker in front of the goal blocks wide shots too.
struct Share {
  double inFrame = 0.0;
  double passing = 0.0;
};

// One direction of the aiming grid: across the goal from the post at the
// lower pitch y, or up it from the ground. The ground is no frame: a shot
// aimed below it skids along it.
struct GridAxis {
  std::size_t count = 0;
  double zoneSize = 0.0;
  bool lowEdgeIsFrame = true;
};

// Where along one axis a shot is aimed, and how far it may stray.
struct AxisAim {
  double aim = 0.0;
  double spread = 0.0;
};

// The share of the zone at `index` along an axis.
[[nodiscard]] Share shareOf(const std::size_t index, const GridAxis& axis,
                            const AxisAim& shot) noexcept {
  const double below =
      spreadBelow(static_cast<double>(index) * axis.zoneSize, shot.aim, shot.spread);
  const double upTo =
      spreadBelow(static_cast<double>(index + 1) * axis.zoneSize, shot.aim, shot.spread);
  const double beyondLow = index == 0 ? 0.0 : below;
  const double beyondHigh = index + 1 == axis.count ? 1.0 : upTo;
  return {.inFrame = upTo - (axis.lowEdgeIsFrame ? below : beyondLow),
          .passing = beyondHigh - beyondLow};
}

// How far to either side the keeper reaches at this height: the ellipse of
// his dive and his jump. Negative above his reach.
[[nodiscard]] double keeperReachAt(const double height, const ShotScoringConfig& scoring) noexcept {
  const double share = height / scoring.keeperJumpReach;
  if (share > 1.0) {
    return -1.0;
  }
  return scoring.keeperDiveReach * std::sqrt(1.0 - (share * share));
}

// Seconds the keeper needs to get a hand to the ball at a point at this
// height: a dive within his reach, taking longer the farther he stretches;
// beyond it, the run until the point is in reach, then the full dive. Empty
// above his reach, or where he cannot run.
[[nodiscard]] std::optional<double> keeperSeconds(const PlayerMatchState& keeper, const Vec2 point,
                                                  const double height, const Pitch& pitch,
                                                  const ShotScoringConfig& scoring) {
  const double reach = keeperReachAt(height, scoring);
  if (reach < 0.0) {
    return std::nullopt;
  }
  const Vec2 offset = point - keeper.position;
  const double distance = offset.length();
  if (distance <= reach) {
    return reach > 0.0 ? scoring.keeperDiveSeconds * (distance / reach) : 0.0;
  }
  const auto run = estimateArrivalSeconds(keeper, point - (offset * (reach / distance)), pitch);
  if (!run) {
    return std::nullopt;
  }
  return *run + scoring.keeperDiveSeconds;
}

// The outcome of a shot at the center of a zone, rising in a straight line
// from the ground at the ball to its height at the goal line.
[[nodiscard]] ZoneOutcome zoneOutcome(const MatchState& state, const Vec2 from, const Vec2 target,
                                      const double height, const std::vector<Remembered>& opponents,
                                      const ShotCandidateRules& rules) {
  const ShotScoringConfig& scoring = rules.scoring;
  const Vec2 offset = target - from;
  const double length = offset.length();
  const Lane lane{.from = from,
                  .direction = length > 0.0 ? offset * (1.0 / length) : Vec2{},
                  .speed = scoring.shotSpeed,
                  .length = length};
  const auto heightAt = [height, length](const double along) { return height * along / length; };

  double unblocked = 1.0;
  double unsaved = 1.0;
  for (const Remembered& opponent : opponents) {
    if (isGoalkeeper(state, opponent.index)) {
      const double margin =
          LaneReach::laneMargin(lane, rules.ball, [&](const Vec2 point, const double along) {
            return keeperSeconds(opponent.player, point, heightAt(along), state.pitch(), scoring);
          });
      unsaved *=
          1.0 - LaneReach::riskOfMargin(margin, opponent.confidence, scoring.saveMarginSeconds);
      continue;
    }
    const double margin =
        LaneReach::laneMargin(lane, rules.ball, [&](const Vec2 point, const double along) {
          return heightAt(along) <= scoring.blockReach
                     ? LaneReach::reachSeconds(opponent.player, point, state.pitch(),
                                               rules.reception.controlRadius)
                     : std::nullopt;
        });
    unblocked *=
        1.0 - LaneReach::riskOfMargin(margin, opponent.confidence, scoring.blockMarginSeconds);
  }
  return {
      .block = 1.0 - unblocked, .save = unblocked * (1.0 - unsaved), .goal = unblocked * unsaved};
}

// Where in a zone he aims: its center, except that a zone on the frame draws
// his aim toward the post or the crossbar, as close as his spread lets the
// shot stay inside -- and never closer than the frame margin.
[[nodiscard]] double aimAlong(const std::size_t index, const GridAxis& axis, const double spread,
                              const ShotScoringConfig& scoring) noexcept {
  const double center = (static_cast<double>(index) + 0.5) * axis.zoneSize;
  const double keepOff = std::max(spread, scoring.frameMargin);
  const double total = static_cast<double>(axis.count) * axis.zoneSize;
  if (index + 1 == axis.count) {
    return std::max(center, total - keepOff);
  }
  if (index == 0 && axis.lowEdgeIsFrame) {
    return std::min(center, keepOff);
  }
  return center;
}

[[nodiscard]] ShotRejection rejectionOf(const ShotCandidate& candidate,
                                        const ShotScoringConfig& scoring) noexcept {
  if (candidate.distance > scoring.maxShotDistance) {
    return ShotRejection::kTooFar;
  }
  if (candidate.opening < scoring.minOpening) {
    return ShotRejection::kTooNarrow;
  }
  if (candidate.blockRisk >= scoring.maxBlockRisk) {
    return ShotRejection::kBlocked;
  }
  if (candidate.goalChance < scoring.minGoalChance) {
    return ShotRejection::kUnlikely;
  }
  return ShotRejection::kValid;
}

}  // namespace

std::string_view shotRejectionName(const ShotRejection rejection) noexcept {
  switch (rejection) {
    case ShotRejection::kValid:
      return "valid";
    case ShotRejection::kTooFar:
      return "too far";
    case ShotRejection::kTooNarrow:
      return "too narrow";
    case ShotRejection::kBlocked:
      return "blocked";
    case ShotRejection::kUnlikely:
      return "unlikely";
  }
  return "unknown";
}

ShotContributions shotContributions(const ShotCandidate& candidate,
                                    const ShotScoringConfig& scoring) noexcept {
  const double loss = std::max(0.0, 1.0 - candidate.goalChance - candidate.secondBallChance);
  return {.goal = scoring.goalWeight * candidate.goalChance,
          .secondBall = scoring.secondBallWeight * candidate.secondBallChance,
          .possession = -(scoring.lossWeight * loss)};
}

std::string_view dominantShotContribution(const ShotContributions& parts) noexcept {
  const std::array<std::pair<std::string_view, double>, 3> named{{
      {"goal", parts.goal},
      {"secondBall", parts.secondBall},
      {"possession", parts.possession},
  }};
  std::size_t dominant = 0;
  for (std::size_t index = 1; index < named.size(); ++index) {
    if (std::abs(named.at(index).second) > std::abs(named.at(dominant).second)) {
      dominant = index;
    }
  }
  return named.at(dominant).first;
}

void validate(const ShotScoringConfig& config) {
  const auto finiteIn = [](const double value, const double min, const double max) {
    return value >= min && value <= max;  // false for NaN
  };
  const auto positive = [](const double value) {
    return value > 0.0 && value <= std::numeric_limits<double>::max();  // false for NaN
  };
  constexpr double kMax = std::numeric_limits<double>::max();
  const bool valid =
      finiteIn(config.minConfidence, 0.0, 1.0) && finiteIn(config.minOpening, 0.0, 2.0) &&
      finiteIn(config.reboundShare, 0.0, 1.0) && finiteIn(config.maxBlockRisk, 0.0, 1.0) &&
      finiteIn(config.minGoalChance, 0.0, 1.0) && config.zoneColumns >= 2 &&
      config.zoneColumns <= kMaxShotZones && config.zoneRows >= 1 &&
      config.zoneRows <= kMaxShotZones && positive(config.maxShotDistance) &&
      positive(config.shotSpeed) && positive(config.keeperDiveReach) &&
      positive(config.keeperJumpReach) && positive(config.keeperDiveSeconds) &&
      positive(config.blockReach) && positive(config.blockMarginSeconds) &&
      positive(config.saveMarginSeconds) && positive(config.spreadAtZero) &&
      finiteIn(config.spreadPerMeter, 0.0, kMax) && positive(config.pressureRadius) &&
      finiteIn(config.pressureSpread, 0.0, kMax) && finiteIn(config.frameMargin, 0.0, kMax) &&
      finiteIn(config.goalWeight, 0.0, kMaxScoringWeight) &&
      finiteIn(config.secondBallWeight, 0.0, kMaxScoringWeight) &&
      finiteIn(config.lossWeight, 0.0, kMaxScoringWeight);
  if (!valid) {
    throw std::invalid_argument("shot scoring: invalid configuration");
  }
}

std::vector<ShotCandidate> generateShotCandidates(const MatchState& state,
                                                  const std::size_t shooterIndex,
                                                  const SimCore::SimTick now,
                                                  const double secondsPerTick,
                                                  const ShotCandidateRules& rules) {
  const ShotScoringConfig& scoring = rules.scoring;
  const PlayerMatchState& shooter = state.players()[shooterIndex];
  const Vec2 from = state.ball().position;
  const Goal goal =
      state.pitch().goal(attackingDirection(shooter.side) > 0.0 ? GoalEnd::kMaxX : GoalEnd::kMinX);
  const auto columns = static_cast<std::size_t>(scoring.zoneColumns);
  const auto rows = static_cast<std::size_t>(scoring.zoneRows);
  const double zoneWidth = goal.widthMeters / static_cast<double>(columns);
  const double zoneHeight = goal.heightMeters / static_cast<double>(rows);
  const GridAxis across{.count = columns, .zoneSize = zoneWidth, .lowEdgeIsFrame = true};
  const GridAxis upward{.count = rows, .zoneSize = zoneHeight, .lowEdgeIsFrame = false};
  const double lowPostY = goal.postAtMinY().y;

  const std::vector<Remembered> opponents =
      LaneReach::rememberedPlayers(state, shooterIndex, now, secondsPerTick, rules.perception,
                                   scoring.minConfidence)
          .opponents;
  double nearestOpponent = std::numeric_limits<double>::infinity();
  for (const Remembered& opponent : opponents) {
    nearestOpponent = std::min(nearestOpponent, (opponent.player.position - from).length());
  }
  const double pressure = std::max(0.0, 1.0 - (nearestOpponent / scoring.pressureRadius));

  const double distance = (goal.center - from).length();
  const double spread = (scoring.spreadAtZero + (scoring.spreadPerMeter * distance)) *
                        (1.0 + (scoring.pressureSpread * pressure)) *
                        accuracyFactor(shooter.attributes.shotAccuracy);

  std::vector<ShotCandidate> candidates;
  candidates.reserve(columns * rows);
  for (std::size_t column = 0; column < columns; ++column) {
    for (std::size_t row = 0; row < rows; ++row) {
      candidates.push_back(ShotCandidate{
          .column = column,
          .row = row,
          .target = {.x = goal.center.x, .y = lowPostY + aimAlong(column, across, spread, scoring)},
          .height = aimAlong(row, upward, spread, scoring),
          .distance = distance,
          .opening = openingOf(from, goal),
          .spread = spread});
    }
  }

  const ShotRejection position = rejectionOf(candidates.front(), scoring);
  const bool scored = position != ShotRejection::kTooFar && position != ShotRejection::kTooNarrow;
  if (scored) {
    // What becomes of a shot in each zone, from its center; where a shot may
    // end up is the same grid.
    std::vector<ZoneOutcome> outcomes;
    outcomes.reserve(columns * rows);
    for (std::size_t column = 0; column < columns; ++column) {
      for (std::size_t row = 0; row < rows; ++row) {
        const Vec2 center{.x = goal.center.x,
                          .y = lowPostY + ((static_cast<double>(column) + 0.5) * zoneWidth)};
        outcomes.push_back(zoneOutcome(
            state, from, center, (static_cast<double>(row) + 0.5) * zoneHeight, opponents, rules));
      }
    }
    for (ShotCandidate& candidate : candidates) {
      for (std::size_t column = 0; column < columns; ++column) {
        const Share horizontal =
            shareOf(column, across, {.aim = candidate.target.y - lowPostY, .spread = spread});
        for (std::size_t row = 0; row < rows; ++row) {
          const Share vertical = shareOf(row, upward, {.aim = candidate.height, .spread = spread});
          const ZoneOutcome& outcome = outcomes[(column * rows) + row];
          const double inFrame = horizontal.inFrame * vertical.inFrame;
          candidate.onTarget += inFrame;
          candidate.blockRisk += horizontal.passing * vertical.passing * outcome.block;
          candidate.saveRisk += inFrame * outcome.save;
          candidate.goalChance += inFrame * outcome.goal;
        }
      }
      candidate.secondBallChance =
          scoring.reboundShare * (candidate.blockRisk + candidate.saveRisk);
      candidate.utility = shotContributions(candidate, scoring).total();
    }
  }
  for (ShotCandidate& candidate : candidates) {
    candidate.rejection = rejectionOf(candidate, scoring);
  }

  std::ranges::sort(candidates, [](const ShotCandidate& left, const ShotCandidate& right) {
    return std::tuple(!left.isValid(), -left.utility, left.column, left.row) <
           std::tuple(!right.isValid(), -right.utility, right.column, right.row);
  });
  return candidates;
}

}  // namespace ElyverseFootball::SimMatch
