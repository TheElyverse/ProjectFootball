#include "crossCandidates.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <optional>
#include <stdexcept>
#include <tuple>
#include <utility>
#include <vector>

#include "configChecks.hpp"
#include "laneReach.hpp"
#include "passCandidates.hpp"
#include "shotCandidates.hpp"
#include "spatialQueries.hpp"
#include "teamFrame.hpp"
#include "zones.hpp"

namespace ElyverseFootball::SimMatch {
namespace {

using ConfigChecks::isFiniteNonNegative;
using ConfigChecks::isPositiveFinite;
using ConfigChecks::isShare;
using ConfigChecks::isWithin;
using LaneReach::Remembered;
using SimCore::Vec2;

constexpr double kNever = std::numeric_limits<double>::infinity();

// The attacked goal and the directions a cross is laid out in: into the pitch
// from its goal line, and across toward the side the crosser is on.
struct Box {
  Goal goal;
  GoalEnd end = GoalEnd::kMaxX;
  Vec2 inward;
  Vec2 toCrosser;
  PitchMarkings markings;
};

// The point `depth` meters into the pitch from the goal's centre and `across`
// meters toward the crosser's side.
[[nodiscard]] Vec2 boxPoint(const Box& box, const double depth, const double across) noexcept {
  return box.goal.center + (box.inward * depth) + (box.toCrosser * across);
}

// The areas of the box, each with where it arrives and how high.
struct Area {
  CrossArea area = CrossArea::kPenaltySpot;
  Vec2 target;
  std::optional<SimCore::PlayerId> runner = std::nullopt;
};

[[nodiscard]] std::vector<Area> areasOf(const MatchState& state, const Box& box,
                                        const std::vector<Remembered>& teammates) {
  const PitchMarkings& marks = box.markings;
  std::vector<Area> areas{
      {.area = CrossArea::kNearPost,
       .target = boxPoint(box, marks.goalAreaDepthMeters, marks.goalWidthMeters / 2.0)},
      {.area = CrossArea::kPenaltySpot, .target = state.pitch().penaltySpot(box.end)},
      {.area = CrossArea::kFarPost,
       .target = boxPoint(box, marks.goalAreaDepthMeters, -marks.goalWidthMeters / 2.0)},
      {.area = CrossArea::kCutBack,
       .target = boxPoint(box, marks.penaltySpotDistanceMeters, marks.goalAreaWidthMeters / 2.0)}};
  // His teammates' runs into the box: where their side sends them, which a
  // team knows (docs/on-ball.md#crossing).
  for (const Remembered& teammate : teammates) {
    const auto& run = state.players()[teammate.index].target;
    if (run && state.pitch().isInPenaltyArea(box.end, *run)) {
      areas.push_back(
          {.area = CrossArea::kRun, .target = *run, .runner = teammate.player.playerId});
    }
  }
  return areas;
}

// Seconds until the ball gets to the target: a flight against the drag, or
// a roll along the ground; infinite if it never does.
[[nodiscard]] double arrivalSeconds(const CrossCandidate& cross, const double length,
                                    const BallPhysics& ball) noexcept {
  return cross.height > 0.0 ? flightSeconds(length, cross.speed, ball)
                            : LaneReach::ballSeconds(length, cross.speed, ball);
}

// What scoring a cross needs: the state, the ball, the box, his curl, how
// wide the goal looks from the penalty spot, and who he remembers.
struct CrossContext {
  const MatchState* state = nullptr;
  Vec2 from;
  Box box;
  double curl = 0.0;
  double spotOpening = 0.0;
  const std::vector<Remembered>* teammates = nullptr;
  const std::vector<Remembered>* opponents = nullptr;
  const CrossCandidateRules* rules = nullptr;
};

// The cross's flight: its curvature toward the goal -- to the side of its
// line the goal's centre is on --, when it arrives, and whether a kick of
// his gets it there.
[[nodiscard]] bool laidOut(const CrossContext& crossing, CrossCandidate& cross) {
  const CrossCandidateRules& rules = (*crossing.rules);
  const bool ground = cross.height <= 0.0;
  const Vec2 chord = cross.target - crossing.from;
  const Vec2 toGoal = crossing.box.goal.center - crossing.from;
  if (!ground) {
    cross.curvature =
        ((chord.x * toGoal.y) - (chord.y * toGoal.x)) >= 0.0 ? crossing.curl : -crossing.curl;
  }
  const ArcAim aim = aimAlongArc(crossing.from, cross.target, cross.curvature);
  cross.curvature = aim.curvature;
  cross.ballSeconds = arrivalSeconds(cross, aim.length, rules.ball);
  const double fastest = ground ? rules.passing.maxSpeed : rules.passing.maxLoftedSpeed;
  if (!(cross.ballSeconds < kNever) || cross.speed > fastest) {
    return false;
  }
  if (ground) {
    return true;
  }
  const auto rise = launchVerticalVelocity(
      {.speed = cross.speed, .distance = aim.length, .height = cross.height}, rules.ball);
  return rise.has_value() && rise.value_or(kNever) <= rules.shooting.maxLaunchSlope * cross.speed;
}

// The remembered teammate who gets to the cross first -- for a run the one
// making it --, and how sure the crosser is of him; 0 without one.
[[nodiscard]] double findRunner(const CrossContext& crossing, CrossCandidate& cross,
                                const std::optional<SimCore::PlayerId> run) {
  double confidence = 0.0;
  cross.runnerSeconds = kNever;
  for (const Remembered& teammate : (*crossing.teammates)) {
    if (run && teammate.player.playerId != *run) {
      continue;
    }
    const double reach =
        LaneReach::reachSeconds(teammate.player, cross.target, (*crossing.state).pitch(),
                                (*crossing.rules).reception.controlRadius)
            .value_or(kNever);
    if (reach < cross.runnerSeconds) {
      cross.runnerSeconds = reach;
      cross.runner = teammate.player.playerId;
      confidence = teammate.confidence;
    }
  }
  return confidence;
}

// The chance no remembered opponent gets there before the runner: each
// counted from no earlier than the ball; the keeper with his hands for a
// cross in the air in his penalty area, his risk the cross's keeperClaim.
[[nodiscard]] double arrives(const CrossContext& crossing, CrossCandidate& cross) {
  const MatchState& state = (*crossing.state);
  const CrossCandidateRules& rules = (*crossing.rules);
  const double contest = rules.scoring.contestMarginSeconds;
  const double first = std::max(cross.runnerSeconds, cross.ballSeconds);
  const bool ground = cross.height <= 0.0;
  const Vec2 chord = cross.target - crossing.from;
  const double length = chord.length();
  const LaneReach::Lane lane{.from = crossing.from,
                             .direction = length > 0.0 ? chord * (1.0 / length) : Vec2{},
                             .speed = cross.speed,
                             .length = length};
  double arriving = 1.0;
  for (const Remembered& opponent : (*crossing.opponents)) {
    const bool keeper = isGoalkeeper(state, opponent.index);
    const bool hands =
        keeper && !ground && state.pitch().isInPenaltyArea(crossing.box.end, cross.target);
    const double radius = hands ? rules.reception.handsRadius : rules.reception.controlRadius;
    const auto reach =
        LaneReach::reachSeconds(opponent.player, cross.target, state.pitch(), radius);
    double risk = reach ? LaneReach::riskOfMargin(std::max(*reach, cross.ballSeconds) - first,
                                                  opponent.confidence, contest)
                        : 0.0;
    // Along the ground he can cut it out on its way, as a pass.
    if (ground) {
      const double wayMargin =
          LaneReach::laneMargin(lane, rules.ball, [&](const Vec2 point, double /*along*/) {
            return LaneReach::reachSeconds(opponent.player, point, state.pitch(),
                                           rules.reception.controlRadius);
          });
      risk = std::max(risk, LaneReach::riskOfMargin(wayMargin, opponent.confidence, contest));
    }
    arriving *= 1.0 - risk;
    if (keeper) {
      cross.keeperClaim = risk;
    }
  }
  // A runner who arrives late finds the ball gone on.
  return arriving * (1.0 - LaneReach::riskOfMargin(
                               cross.ballSeconds - cross.runnerSeconds + contest, 1.0, contest));
}

// How a cross is delivered: its speed along the ground and the height it
// arrives at.
struct Delivery {
  double speed = 0.0;
  double height = 0.0;
};

// The cross at an area with this delivery, scored and judged valid or not.
[[nodiscard]] CrossCandidate scoredCross(const CrossContext& crossing, const Area& area,
                                         const Delivery& delivery) {
  const CrossScoringConfig& scoring = (*crossing.rules).scoring;
  CrossCandidate cross{.area = area.area,
                       .target = area.target,
                       .height = delivery.height,
                       .speed = delivery.speed,
                       .curvature = 0.0,
                       .runner = area.runner};
  const bool reachable = laidOut(crossing, cross);
  const double runnerConfidence = findRunner(crossing, cross, area.runner);
  const bool runner = cross.runner.has_value() && cross.runnerSeconds < kNever;
  // A cross that never gets there wins nothing, and keeps the scores finite.
  cross.winChance = runner && reachable ? arrives(crossing, cross) * runnerConfidence : 0.0;
  cross.threat =
      crossing.spotOpening > 0.0
          ? std::min(1.0, goalOpening(cross.target, crossing.box.goal) / crossing.spotOpening)
          : 0.0;
  cross.utility = crossContributions(cross, scoring).total();
  if (!runner) {
    cross.rejection = CrossRejection::kNoRunner;
  } else if (!reachable) {
    cross.rejection = CrossRejection::kOutOfReach;
  } else if (cross.winChance < scoring.minWinChance) {
    cross.rejection = CrossRejection::kUnlikely;
  }
  return cross;
}

}  // namespace

std::string_view crossAreaName(const CrossArea area) noexcept {
  switch (area) {
    case CrossArea::kNearPost:
      return "nearPost";
    case CrossArea::kPenaltySpot:
      return "penaltySpot";
    case CrossArea::kFarPost:
      return "farPost";
    case CrossArea::kCutBack:
      return "cutBack";
    case CrossArea::kRun:
      return "run";
  }
  return "unknown";
}

std::string_view crossRejectionName(const CrossRejection rejection) noexcept {
  switch (rejection) {
    case CrossRejection::kValid:
      return "valid";
    case CrossRejection::kNoRunner:
      return "noRunner";
    case CrossRejection::kOutOfReach:
      return "outOfReach";
    case CrossRejection::kUnlikely:
      return "unlikely";
  }
  return "unknown";
}

void validate(const CrossScoringConfig& config) {
  const bool valid =
      isShare(config.minConfidence) && isShare(config.maxDepthShare) && isShare(config.curlShare) &&
      isShare(config.minWinChance) && isPositiveFinite(config.crossSpeed) &&
      isPositiveFinite(config.headerHeight) && isPositiveFinite(config.drivenSpeed) &&
      isPositiveFinite(config.drivenHeight) && isPositiveFinite(config.cutBackSpeed) &&
      isPositiveFinite(config.contestMarginSeconds) &&
      isWithin(config.winWeight, 0.0, kMaxScoringWeight) &&
      isWithin(config.threatWeight, 0.0, kMaxScoringWeight) &&
      isWithin(config.lossWeight, 0.0, kMaxScoringWeight);
  if (!valid) {
    throw std::invalid_argument("cross scoring: invalid configuration");
  }
}

CrossContributions crossContributions(const CrossCandidate& candidate,
                                      const CrossScoringConfig& scoring) noexcept {
  return {.win = scoring.winWeight * candidate.winChance,
          .threat = scoring.threatWeight * candidate.winChance * candidate.threat,
          .loss = -(scoring.lossWeight * (1.0 - candidate.winChance))};
}

std::string_view dominantCrossContribution(const CrossContributions& parts) noexcept {
  const std::array<std::pair<std::string_view, double>, 3> named{
      {{"win", parts.win}, {"threat", parts.threat}, {"loss", parts.loss}}};
  const auto* largest = std::ranges::max_element(named, [](const auto& left, const auto& right) {
    return std::abs(left.second) < std::abs(right.second);
  });
  return largest->first;
}

CrossScoringConfig crossScoringForRisk(const CrossScoringConfig& scoring,
                                       const double passingRisk) noexcept {
  CrossScoringConfig adjusted = scoring;
  adjusted.threatWeight = scoring.threatWeight * (0.5 + passingRisk);
  adjusted.lossWeight = scoring.lossWeight * (1.5 - passingRisk);
  adjusted.minWinChance = std::min(1.0, scoring.minWinChance * (1.3 - (0.6 * passingRisk)));
  return adjusted;
}

std::vector<CrossCandidate> generateCrossCandidates(const MatchState& state,
                                                    const std::size_t carrierIndex,
                                                    const SimCore::StepTime time,
                                                    const CrossCandidateRules& rules) {
  const CrossScoringConfig& scoring = rules.scoring;
  const PlayerMatchState& carrier = state.players()[carrierIndex];
  const Pitch& pitch = state.pitch();
  const Vec2 from = state.ball().position;
  const Goal goal = attackedGoal(pitch, carrier.side);
  const double direction = attackingDirection(carrier.side);
  const Box box{.goal = goal,
                .end = direction > 0.0 ? GoalEnd::kMaxX : GoalEnd::kMinX,
                .inward = {.x = -direction, .y = 0.0},
                .toCrosser = {.x = 0.0, .y = from.y >= goal.center.y ? 1.0 : -1.0},
                .markings = pitch.markings()};
  const double depthToGoalLine = std::abs(goal.center.x - from.x);
  const bool wide = std::abs(from.y - goal.center.y) > box.markings.penaltyAreaWidthMeters / 2.0;
  if (!scoring.enabled || !wide || depthToGoalLine > scoring.maxDepthShare * pitch.lengthMeters()) {
    return {};
  }

  const auto [teammates, opponents] = LaneReach::rememberedPlayers(
      state, carrierIndex, time, rules.perception, scoring.minConfidence);
  const double curl = rules.ball.magnusCurvature * rules.shooting.curlSpin *
                      carrier.attributes.shotTechnique * scoring.curlShare;
  const double spotOpening = goalOpening(pitch.penaltySpot(box.end), goal);

  const CrossContext crossing{.state = &state,
                              .from = from,
                              .box = box,
                              .curl = curl,
                              .spotOpening = spotOpening,
                              .teammates = &teammates,
                              .opponents = &opponents,
                              .rules = &rules};
  std::vector<CrossCandidate> candidates;
  for (const Area& area : areasOf(state, box, teammates)) {
    if (area.area == CrossArea::kCutBack) {
      candidates.push_back(
          scoredCross(crossing, area, {.speed = scoring.cutBackSpeed, .height = 0.0}));
      continue;
    }
    // Floated to the head, or driven low and fast.
    candidates.push_back(
        scoredCross(crossing, area, {.speed = scoring.crossSpeed, .height = scoring.headerHeight}));
    candidates.push_back(scoredCross(
        crossing, area, {.speed = scoring.drivenSpeed, .height = scoring.drivenHeight}));
  }

  std::ranges::sort(candidates, [](const CrossCandidate& left, const CrossCandidate& right) {
    const auto runnerOf = [](const CrossCandidate& cross) {
      return cross.runner.value_or(SimCore::PlayerId::invalid()).value();
    };
    return std::tuple(!left.isValid(), -left.utility, left.area, runnerOf(left), left.speed) <
           std::tuple(!right.isValid(), -right.utility, right.area, runnerOf(right), right.speed);
  });
  return candidates;
}

}  // namespace ElyverseFootball::SimMatch
