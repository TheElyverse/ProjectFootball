#include "shotCandidates.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <optional>
#include <span>
#include <stdexcept>
#include <tuple>
#include <utility>

#include "carrierScan.hpp"
#include "configChecks.hpp"
#include "laneReach.hpp"
#include "passCandidates.hpp"
#include "pitch.hpp"
#include "shooting.hpp"
#include "spatialQueries.hpp"
#include "teamFrame.hpp"
#include "zones.hpp"

namespace ElyverseFootball::SimMatch {
namespace {

using ConfigChecks::isFiniteNonNegative;
using ConfigChecks::isPositiveFinite;
using ConfigChecks::isShare;
using ConfigChecks::isWithin;
using LaneReach::Lane;
using LaneReach::Remembered;
using SimCore::Vec2;

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

// Seconds an outfield player needs to get his body within blockRadius of the
// ball at a point: none if he already stands there -- a blocker in the way
// need not move --, otherwise the run until he is.
[[nodiscard]] std::optional<double> blockSeconds(const PlayerMatchState& blocker, const Vec2 point,
                                                 const Pitch& pitch,
                                                 const ShotCandidateRules& rules) {
  const double radius = rules.scoring.blockRadius;
  if ((point - blocker.position).length() <= radius) {
    return 0.0;
  }
  return LaneReach::reachSeconds(blocker, point, pitch, radius);
}

// How a shot of one type travels as the shooter imagines it: its speed, and
// the curvature of its path (docs/on-ball.md#shot-types).
struct ShotPath {
  ShotType type = ShotType::kDriven;
  double speed = 0.0;
  double curvature = 0.0;
};

// The ways he can strike the ball: driven, curled either way -- as far as his
// technique curls it -- and chipped.
[[nodiscard]] std::vector<ShotPath> shotPaths(const PlayerMatchState& shooter,
                                              const ShotCandidateRules& rules) {
  const ShotScoringConfig& scoring = rules.scoring;
  std::vector<ShotPath> paths{{.type = ShotType::kDriven, .speed = scoring.shotSpeed}};
  const double curl =
      rules.ball.magnusCurvature * scoring.curlSpin * shooter.attributes.shotTechnique;
  if (curl > 0.0) {
    paths.push_back({.type = ShotType::kCurled, .speed = scoring.curledSpeed, .curvature = curl});
    paths.push_back({.type = ShotType::kCurled, .speed = scoring.curledSpeed, .curvature = -curl});
  }
  paths.push_back({.type = ShotType::kChipped, .speed = scoring.chippedSpeed});
  return paths;
}

// The flight a chip takes to a point `length` meters on at `height`: the
// launch its speed along the ground asks for (launchVerticalVelocity()), from
// the ground at the ball; empty for any other shot, which the shooter imagines
// rising in a straight line, and for a chip that would have to rise steeper
// than maxLaunchSlope (rejectUnreachableChip()).
[[nodiscard]] std::optional<BallState> chipFlight(const ShotPath& path, const Lane& lane,
                                                  const double height,
                                                  const ShotCandidateRules& rules) {
  if (path.type != ShotType::kChipped) {
    return std::nullopt;
  }
  const auto rise = launchVerticalVelocity(
      {.speed = path.speed, .distance = lane.length, .height = height}, rules.ball);
  if (!rise || *rise > rules.scoring.maxLaunchSlope * path.speed) {
    return std::nullopt;
  }
  BallState flight{.position = lane.from,
                   .velocity = lane.direction * path.speed,
                   .owner = std::nullopt,
                   .lastTouch = std::nullopt};
  flight.verticalVelocity = *rise;
  return flight;
}

// How much further than a driven shot a shot of this type strays, for a
// player of average technique (docs/on-ball.md#shot-types): a curled shot by
// how far its sidespin's error bends it off at the goal, k·L²/2 for the
// error's curvature k -- it leaves along the curve it planned --; a
// chip by the height its pace's error costs where it comes down -- the
// distance it flies grows with the square of its pace, and it falls at the
// slope its parabola has at the goal.
[[nodiscard]] double techniqueSpread(const ShotPath& path, const double distance,
                                     const ShotScoringConfig& scoring,
                                     const BallPhysics& ball) noexcept {
  if (path.type == ShotType::kCurled) {
    return ball.magnusCurvature * scoring.spinError * distance * distance / 2.0;
  }
  if (path.type == ShotType::kChipped) {
    // The slope of the drag-free parabola it comes down on.
    const double descent = ball.gravity * distance / (2.0 * path.speed * path.speed);
    return 2.0 * scoring.speedError * distance * descent;
  }
  return 0.0;
}

// Rejects a chip from `from` that cannot get to its aim, as kTooFar: it
// would have to rise steeper than maxLaunchSlope at its speed along the
// ground. Any other shot is left as it is.
void rejectUnreachableChip(const Vec2 from, ShotCandidate& shot, const ShotCandidateRules& rules) {
  if (shot.type != ShotType::kChipped) {
    return;
  }
  const auto rise = launchVerticalVelocity(
      {.speed = shot.speed, .distance = (shot.target - from).length(), .height = shot.height},
      rules.ball);
  if (!rise || *rise > rules.scoring.maxLaunchSlope * shot.speed) {
    shot.rejection = ShotRejection::kTooFar;
  }
}

// The outcome of a shot of one type at the center of a zone, along its path
// from the ball to its height at the goal line.
[[nodiscard]] ZoneOutcome zoneOutcome(const MatchState& state, const Vec2 from, const Vec2 target,
                                      const double height, const std::vector<Remembered>& opponents,
                                      const ShotCandidateRules& rules, const ShotPath& path) {
  const ShotScoringConfig& scoring = rules.scoring;
  const ArcAim aim = aimAlongArc(from, target, path.curvature);
  const double length = aim.length;
  const Lane lane{.from = from,
                  .direction = length > 0.0 ? aim.direction : Vec2{},
                  .speed = path.speed,
                  .length = length,
                  .curvature = aim.curvature,
                  .flying = path.type == ShotType::kChipped};
  // The points along the way and how high the ball is at each, once for
  // every opponent.
  const std::vector<LaneReach::LaneSample> samples = LaneReach::laneSamples(lane, rules.ball);
  const std::optional<BallState> chip = chipFlight(path, lane, height, rules);
  std::vector<double> heights;
  heights.reserve(samples.size());
  for (const LaneReach::LaneSample& sample : samples) {
    heights.push_back(chip ? ballHeightAfter(*chip, rules.ball, sample.ballSeconds)
                           : height * sample.along / length);
  }

  double unblocked = 1.0;
  double unsaved = 1.0;
  for (const Remembered& opponent : opponents) {
    if (isGoalkeeper(state, opponent.index)) {
      const double margin = LaneReach::samplesMargin(
          samples, [&](const LaneReach::LaneSample& sample, const std::size_t index) {
            return keeperSeconds(opponent.player, sample.point, heights[index], state.pitch(),
                                 scoring);
          });
      unsaved *=
          1.0 - LaneReach::riskOfMargin(margin, opponent.confidence, scoring.saveMarginSeconds);
      continue;
    }
    const double margin = LaneReach::samplesMargin(
        samples, [&](const LaneReach::LaneSample& sample, const std::size_t index) {
          return heights[index] <= scoring.blockReach
                     ? blockSeconds(opponent.player, sample.point, state.pitch(), rules)
                     : std::nullopt;
        });
    unblocked *=
        1.0 - LaneReach::riskOfMargin(margin, opponent.confidence, scoring.blockMarginSeconds);
  }
  return {
      .block = 1.0 - unblocked, .save = unblocked * (1.0 - unsaved), .goal = unblocked * unsaved};
}

// The aiming grid's two directions.
struct AimingGrid {
  GridAxis across;
  GridAxis upward;
};

// Adds up a candidate's chances: every zone's outcome -- `outcomes` in column
// order, then row --, weighted by the share of his spread that ends up in it
// or, for a block, passes it.
void addChances(ShotCandidate& candidate, const std::span<const ZoneOutcome> outcomes,
                const AimingGrid& grid, const double lowPostY) noexcept {
  const double spread = candidate.spread;
  for (std::size_t column = 0; column < grid.across.count; ++column) {
    const Share horizontal =
        shareOf(column, grid.across, {.aim = candidate.target.y - lowPostY, .spread = spread});
    for (std::size_t row = 0; row < grid.upward.count; ++row) {
      const Share vertical = shareOf(row, grid.upward, {.aim = candidate.height, .spread = spread});
      const ZoneOutcome& outcome = outcomes[(column * grid.upward.count) + row];
      const double inFrame = horizontal.inFrame * vertical.inFrame;
      candidate.onTarget += inFrame;
      candidate.blockRisk += horizontal.passing * vertical.passing * outcome.block;
      candidate.saveRisk += inFrame * outcome.save;
      candidate.goalChance += inFrame * outcome.goal;
    }
  }
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

// Why no shot can be taken from where the shooter stands, whatever he aims
// at -- the same for every candidate; kValid if one can.
[[nodiscard]] ShotRejection positionRejection(const PlayerMatchState& shooter, const Goal& goal,
                                              const ShotCandidate& shot,
                                              const ShotCandidateRules& rules) noexcept {
  if (shot.distance > rules.scoring.maxShotDistance) {
    return ShotRejection::kTooFar;
  }
  if (!canSee(shooter, goal.center, rules.perception)) {
    return ShotRejection::kGoalUnseen;
  }
  if (shot.opening < rules.scoring.minOpening) {
    return ShotRejection::kTooNarrow;
  }
  return ShotRejection::kValid;
}

[[nodiscard]] ShotRejection rejectionOf(const ShotCandidate& candidate,
                                        const ShotScoringConfig& scoring) noexcept {
  if (candidate.judgedBlockRisk >= scoring.maxBlockRisk) {
    return ShotRejection::kBlocked;
  }
  if (candidate.judgedGoalChance < scoring.minGoalChance) {
    return ShotRejection::kUnlikely;
  }
  if (candidate.utility <= 0.0) {
    return ShotRejection::kNotWorthIt;
  }
  return ShotRejection::kValid;
}

}  // namespace

double goalOpening(const Vec2 from, const Goal& goal) noexcept {
  const Vec2 toMin = goal.postAtMinY() - from;
  const Vec2 toMax = goal.postAtMaxY() - from;
  const double minLength = toMin.length();
  const double maxLength = toMax.length();
  if (minLength <= 0.0 || maxLength <= 0.0) {
    return 0.0;
  }
  return ((toMin * (1.0 / minLength)) - (toMax * (1.0 / maxLength))).length();
}

std::string_view shotTypeName(const ShotType type) noexcept {
  switch (type) {
    case ShotType::kDriven:
      return "driven";
    case ShotType::kCurled:
      return "curled";
    case ShotType::kChipped:
      return "chipped";
  }
  return "unknown";
}

std::string_view shotRejectionName(const ShotRejection rejection) noexcept {
  switch (rejection) {
    case ShotRejection::kValid:
      return "valid";
    case ShotRejection::kTooFar:
      return "too far";
    case ShotRejection::kGoalUnseen:
      return "goal unseen";
    case ShotRejection::kTooNarrow:
      return "too narrow";
    case ShotRejection::kBlocked:
      return "blocked";
    case ShotRejection::kUnlikely:
      return "unlikely";
    case ShotRejection::kNotWorthIt:
      return "not worth it";
  }
  return "unknown";
}

ShotContributions shotContributions(const ShotCandidate& candidate,
                                    const ShotScoringConfig& scoring) noexcept {
  const double loss = std::max(0.0, 1.0 - candidate.judgedGoalChance - candidate.secondBallChance);
  return {.goal = scoring.goalWeight * candidate.judgedGoalChance,
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
  const bool valid =
      isShare(config.minConfidence) && isWithin(config.minOpening, 0.0, 2.0) &&
      isShare(config.reboundShare) && isShare(config.maxBlockRisk) &&
      isShare(config.minGoalChance) && config.zoneColumns >= 2 &&
      config.zoneColumns <= kMaxShotZones && config.zoneRows >= 1 &&
      config.zoneRows <= kMaxShotZones && isPositiveFinite(config.maxShotDistance) &&
      isPositiveFinite(config.shotSpeed) && isPositiveFinite(config.curledSpeed) &&
      isPositiveFinite(config.chippedSpeed) && isFiniteNonNegative(config.curlSpin) &&
      isPositiveFinite(config.keeperDiveReach) && isPositiveFinite(config.keeperJumpReach) &&
      isPositiveFinite(config.keeperDiveSeconds) && isPositiveFinite(config.blockRadius) &&
      isPositiveFinite(config.blockReach) && isPositiveFinite(config.blockMarginSeconds) &&
      isPositiveFinite(config.saveMarginSeconds) && isPositiveFinite(config.spreadAtZero) &&
      isFiniteNonNegative(config.spreadPerMeter) && isFiniteNonNegative(config.spinError) &&
      isFiniteNonNegative(config.speedError) && isPositiveFinite(config.maxLaunchSlope) &&
      isPositiveFinite(config.pressureRadius) && isFiniteNonNegative(config.pressureSpread) &&
      isFiniteNonNegative(config.frameMargin) &&
      isWithin(config.goalWeight, 0.0, kMaxScoringWeight) &&
      isWithin(config.secondBallWeight, 0.0, kMaxScoringWeight) &&
      isWithin(config.lossWeight, 0.0, kMaxScoringWeight);
  if (!valid) {
    throw std::invalid_argument("shot scoring: invalid configuration");
  }
}

std::vector<ShotCandidate> generateShotCandidates(const MatchState& state,
                                                  const std::size_t shooterIndex,
                                                  const SimCore::StepTime time,
                                                  const ShotCandidateRules& rules) {
  const ShotScoringConfig& scoring = rules.scoring;
  const PlayerMatchState& shooter = state.players()[shooterIndex];
  const Vec2 from = state.ball().position;
  const Goal goal = attackedGoal(state.pitch(), shooter.side);
  const auto columns = static_cast<std::size_t>(scoring.zoneColumns);
  const auto rows = static_cast<std::size_t>(scoring.zoneRows);
  const double zoneWidth = goal.widthMeters / static_cast<double>(columns);
  const double zoneHeight = goal.heightMeters / static_cast<double>(rows);
  const GridAxis across{.count = columns, .zoneSize = zoneWidth, .lowEdgeIsFrame = true};
  const GridAxis upward{.count = rows, .zoneSize = zoneHeight, .lowEdgeIsFrame = false};
  const double lowPostY = goal.postAtMinY().y;

  const std::vector<Remembered> opponents =
      LaneReach::rememberedPlayers(state, shooterIndex, time, rules.perception,
                                   scoring.minConfidence)
          .opponents;
  double nearestOpponent = std::numeric_limits<double>::infinity();
  for (const Remembered& opponent : opponents) {
    nearestOpponent = std::min(nearestOpponent, (opponent.player.position - from).length());
  }
  const double pressure = std::max(0.0, 1.0 - (nearestOpponent / scoring.pressureRadius));

  const double distance = (goal.center - from).length();
  const double opening = goalOpening(from, goal);
  const double pressured = 1.0 + (scoring.pressureSpread * pressure);
  const double baseSpread = (scoring.spreadAtZero + (scoring.spreadPerMeter * distance)) *
                            pressured * skillErrorFactor(shooter.attributes.shotAccuracy);
  const double technique = skillErrorFactor(shooter.attributes.shotTechnique) * pressured;

  const std::vector<ShotPath> paths = shotPaths(shooter, rules);
  std::vector<ShotCandidate> candidates;
  candidates.reserve(columns * rows * paths.size());
  for (const ShotPath& path : paths) {
    const double spread =
        baseSpread + (techniqueSpread(path, distance, scoring, rules.ball) * technique);
    for (std::size_t column = 0; column < columns; ++column) {
      for (std::size_t row = 0; row < rows; ++row) {
        candidates.push_back(
            ShotCandidate{.column = column,
                          .row = row,
                          .type = path.type,
                          .speed = path.speed,
                          .curvature = path.curvature,
                          .target = {.x = goal.center.x,
                                     .y = lowPostY + aimAlong(column, across, spread, scoring)},
                          .height = aimAlong(row, upward, spread, scoring),
                          .distance = distance,
                          .opening = opening,
                          .spread = spread});
      }
    }
  }

  // He sees the goal, like everything else, along where he looks.
  PlayerMatchState looking = shooter;
  looking.facing = lookDirection(state, shooterIndex);
  const ShotRejection position = positionRejection(looking, goal, candidates.front(), rules);
  if (position == ShotRejection::kValid) {
    // What becomes of a shot of each type in each zone, from its center;
    // where a shot may end up is the same grid.
    std::vector<ZoneOutcome> outcomes;
    outcomes.reserve(candidates.size());
    for (const ShotPath& path : paths) {
      for (std::size_t column = 0; column < columns; ++column) {
        for (std::size_t row = 0; row < rows; ++row) {
          const Vec2 center{.x = goal.center.x,
                            .y = lowPostY + ((static_cast<double>(column) + 0.5) * zoneWidth)};
          outcomes.push_back(zoneOutcome(state, from, center,
                                         (static_cast<double>(row) + 0.5) * zoneHeight, opponents,
                                         rules, path));
        }
      }
    }
    const std::size_t zones = columns * rows;
    for (std::size_t index = 0; index < candidates.size(); ++index) {
      ShotCandidate& candidate = candidates[index];
      // The outcomes of the candidate's own type: the candidates come in the
      // same order, one block of zones per path.
      const std::size_t first = index - (index % zones);
      addChances(candidate, std::span(outcomes).subspan(first, zones),
                 {.across = across, .upward = upward}, lowPostY);
      judgeShotCandidate(
          candidate,
          {.block = candidate.blockRisk, .save = candidate.saveRisk, .goal = candidate.goalChance},
          scoring);
      rejectUnreachableChip(from, candidate, rules);
    }
  } else {
    for (ShotCandidate& candidate : candidates) {
      candidate.rejection = position;
    }
  }
  orderShotCandidates(candidates);
  return candidates;
}

void judgeShotCandidate(ShotCandidate& candidate, const ShotChances& judged,
                        const ShotScoringConfig& scoring) noexcept {
  candidate.judgedBlockRisk = judged.block;
  candidate.judgedSaveRisk = judged.save;
  candidate.judgedGoalChance = judged.goal;
  candidate.secondBallChance = scoring.reboundShare * (judged.block + judged.save);
  candidate.utility = shotContributions(candidate, scoring).total();
  if (isScored(candidate.rejection)) {
    candidate.rejection = rejectionOf(candidate, scoring);
  }
}

void orderShotCandidates(std::vector<ShotCandidate>& candidates) {
  std::ranges::sort(candidates, [](const ShotCandidate& left, const ShotCandidate& right) {
    return std::tuple(!left.isValid(), -left.utility, left.column, left.row, left.type,
                      -left.curvature) < std::tuple(!right.isValid(), -right.utility, right.column,
                                                    right.row, right.type, -right.curvature);
  });
}

}  // namespace ElyverseFootball::SimMatch
