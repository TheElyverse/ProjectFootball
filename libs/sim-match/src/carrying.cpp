#include "carrying.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <tuple>
#include <utility>

#include "laneReach.hpp"
#include "passCandidates.hpp"
#include "playerMovement.hpp"
#include "shotCandidates.hpp"
#include "spatialQueries.hpp"
#include "zones.hpp"

namespace ElyverseFootball::SimMatch {
namespace {

using SimCore::Vec2;

using LaneReach::Lane;
using LaneReach::Remembered;

// Spacing of the points along a carry where the players' reach is checked.
constexpr double kCarryStep = 1.0;  // m

[[nodiscard]] double cross(const Vec2 left, const Vec2 right) noexcept {
  return (left.x * right.y) - (left.y * right.x);
}

[[nodiscard]] double lerp(const double atZero, const double atOne, const double share) noexcept {
  return atZero + ((atOne - atZero) * share);
}

// Which half of the turn a direction lies in, counterclockwise from +x: 0 for
// angles in [0, pi), 1 for [pi, 2 pi).
[[nodiscard]] int halfOf(const Vec2 direction) noexcept {
  return direction.y < 0.0 || (direction.y == 0.0 && direction.x < 0.0) ? 1 : 0;
}

// Whether `left` comes before `right` counterclockwise from +x: exact, from
// the half-planes and the cross product, without trigonometry.
[[nodiscard]] bool turnsBefore(const Vec2 left, const Vec2 right) noexcept {
  const int leftHalf = halfOf(left);
  const int rightHalf = halfOf(right);
  if (leftHalf != rightHalf) {
    return leftHalf < rightHalf;
  }
  return cross(left, right) > 0.0;
}

// The middle of the gap from `from` counterclockwise to `until`, both unit
// vectors; empty for no gap at all, two opponents in the same direction.
[[nodiscard]] std::optional<Vec2> gapMiddle(const Vec2 from, const Vec2 until) noexcept {
  const double turn = cross(from, until);
  if (turn > 0.0) {
    return SimCore::unitOr(from + until, from);
  }
  if (turn < 0.0) {
    return SimCore::unitOr((from + until) * -1.0, from);
  }
  if (from.dot(until) > 0.0) {
    return std::nullopt;
  }
  return Vec2{.x = -from.y, .y = from.x};
}

// The directions through the middles of the gaps between the opponents
// around `from`: one gap behind a lone opponent, none without one.
[[nodiscard]] std::vector<Vec2> gapDirections(const Vec2 from,
                                              const std::vector<Remembered>& opponents,
                                              const double radius) {
  std::vector<Vec2> around;
  for (const Remembered& opponent : opponents) {
    const Vec2 offset = opponent.player.position - from;
    if (const double distance = offset.length(); distance > 0.0 && distance <= radius) {
      around.push_back(offset * (1.0 / distance));
    }
  }
  std::ranges::sort(around, turnsBefore);
  std::vector<Vec2> gaps;
  if (around.size() == 1) {
    gaps.push_back(around.front() * -1.0);
  }
  for (std::size_t index = 0; around.size() > 1 && index < around.size(); ++index) {
    if (const auto middle = gapMiddle(around[index], around[(index + 1) % around.size()])) {
      gaps.push_back(*middle);
    }
  }
  return gaps;
}

// The direction toward the cell his team controls best within `reach` of
// `from` -- ties to the cell farther up the pitch, then to the first --, at
// least minDistance away; empty without a grid or such a cell.
[[nodiscard]] std::optional<Vec2> spaceDirection(const MatchState& state, const Vec2 from,
                                                 const TeamSide side, const double reach,
                                                 const double minDistance) {
  const auto& grid = state.pitchControl();
  if (!grid) {
    return std::nullopt;
  }
  const double forward = attackingDirection(side);
  std::optional<std::tuple<double, double, Vec2>> best;
  for (std::size_t column = 0; column < grid->columns(); ++column) {
    for (std::size_t row = 0; row < grid->rows(); ++row) {
      const GridCell cell{.column = column, .row = row};
      const Vec2 center = grid->cellCenter(cell);
      const double distance = SimCore::distance(from, center);
      if (distance < minDistance || distance > reach) {
        continue;
      }
      const std::tuple<double, double, Vec2> scored{grid->control(side, cell), forward * center.x,
                                                    center};
      if (!best || std::get<0>(scored) > std::get<0>(*best) ||
          (std::get<0>(scored) == std::get<0>(*best) && std::get<1>(scored) > std::get<1>(*best))) {
        best = scored;
      }
    }
  }
  if (!best) {
    return std::nullopt;
  }
  return SimCore::unitOr(std::get<2>(*best) - from, Vec2{.x = forward, .y = 0.0});
}

// How good a shot from this point is for a player of `side`, in [0, 1]: the
// goal's opening over fullOpening, at most 1, falling linearly with the
// distance to the goal's centre to 0 at shotDistance.
[[nodiscard]] double shotValue(const Vec2 from, const Goal& goal,
                               const CarryScoringConfig& scoring) noexcept {
  const double distance = SimCore::distance(from, goal.center);
  if (distance >= scoring.shotDistance) {
    return 0.0;
  }
  const double opening = std::min(1.0, goalOpening(from, goal) / scoring.fullOpening);
  return opening * (1.0 - (distance / scoring.shotDistance));
}

// The carrier as he imagines himself with the ball: at his running speed
// with it.
[[nodiscard]] PlayerMatchState carrierRunning(const MatchState& state, const std::size_t index,
                                              const BallPhysics& physics) {
  PlayerMatchState carrier = atRunningSpeed(state.players()[index], state.ball(), physics);
  carrier.target = std::nullopt;
  return carrier;
}

// How far he can carry the ball along `direction` within horizonSeconds:
// short of the first point an opponent gets to within safetySeconds of him,
// and no farther than pitchMargin inside the lines.
[[nodiscard]] double safeDistance(const PlayerMatchState& carrier, const Vec2 direction,
                                  const std::vector<Remembered>& opponents, const Pitch& pitch,
                                  const CarryCandidateRules& rules) {
  const CarryScoringConfig& scoring = rules.scoring;
  const double horizon = carrier.attributes.maxSpeed * scoring.horizonSeconds;
  const double margin = scoring.pitchMargin;
  const auto inside = [&](const Vec2 point) {
    return point.x >= margin && point.x <= pitch.lengthMeters() - margin && point.y >= margin &&
           point.y <= pitch.widthMeters() - margin;
  };
  double safe = 0.0;
  // The samples are the running sum of kCarryStep, deterministic on every
  // platform.
  // NOLINTNEXTLINE(bugprone-float-loop-counter)
  for (double along = kCarryStep; along <= horizon; along += kCarryStep) {
    const Vec2 point = carrier.position + (direction * along);
    if (!inside(point)) {
      break;
    }
    const auto mine = estimateArrivalSeconds(carrier, point, pitch);
    if (!mine) {
      break;
    }
    const bool reached = std::ranges::any_of(opponents, [&](const Remembered& opponent) {
      const auto theirs =
          LaneReach::reachSeconds(opponent.player, point, pitch, rules.reception.controlRadius);
      return theirs && *theirs < *mine + scoring.safetySeconds;
    });
    if (reached) {
      break;
    }
    safe = along;
  }
  return safe;
}

// The touch that sends the ball from where it lies at his feet to meet him
// at `meet`: the speed that rolls it there in the time he needs, and the
// earliest he plays it again, touchShare of that time -- planShare times as
// long, for a dribble.
void planTouch(CarryCandidate& candidate, const PlayerMatchState& carrier, const Vec2 from,
               const Vec2 meet, const double planShare, const Pitch& pitch,
               const CarryCandidateRules& rules) {
  const double seconds =
      std::max(estimateArrivalSeconds(carrier, meet, pitch).value_or(0.0) * planShare,
               std::numeric_limits<double>::min());
  const double distance = SimCore::distance(from, meet);
  candidate.touchTarget = meet;
  // d = v·t − a·t²/2 for the rolling ball.
  candidate.touchSpeed =
      std::min((distance / seconds) + (rules.ball.rollingDeceleration * seconds / 2.0),
               rules.passing.maxSpeed);
  candidate.touchSeconds = seconds * rules.scoring.touchShare;
}

// The risk that a remembered opponent gets to the touched ball before he
// does: one minus the chance that none of them does.
[[nodiscard]] double touchRisk(const CarryCandidate& candidate, const Vec2 from,
                               const std::vector<Remembered>& opponents, const Pitch& pitch,
                               const CarryCandidateRules& rules) {
  const Vec2 offset = candidate.touchTarget - from;
  const double length = offset.length();
  if (length == 0.0) {
    return 0.0;
  }
  const Lane lane{.from = from,
                  .direction = offset * (1.0 / length),
                  .speed = candidate.touchSpeed,
                  .length = length};
  double keeps = 1.0;
  for (const Remembered& opponent : opponents) {
    const double margin =
        LaneReach::laneMargin(lane, rules.ball, [&](const Vec2 point, double /*along*/) {
          return LaneReach::reachSeconds(opponent.player, point, pitch,
                                         rules.reception.controlRadius);
        });
    keeps *=
        1.0 - LaneReach::riskOfMargin(margin, opponent.confidence, rules.scoring.lossMarginSeconds);
  }
  return 1.0 - keeps;
}

// Remembered outfield opponents between `from` and the goal his side attacks,
// along the attacking direction, that are no longer between `target` and it.
[[nodiscard]] int bypassedBy(const MatchState& state, const Vec2 from, const Vec2 target,
                             const TeamSide side, const std::vector<Remembered>& opponents) {
  const double forward = attackingDirection(side);
  int bypassed = 0;
  for (const Remembered& opponent : opponents) {
    const double depth = forward * opponent.player.position.x;
    if (!isGoalkeeper(state, opponent.index) && depth > forward * from.x &&
        depth <= forward * target.x) {
      ++bypassed;
    }
  }
  return bypassed;
}

// Scores a candidate whose kind, target, distance and touch are set, from
// `from` where the ball lies.
void scoreCandidate(CarryCandidate& candidate, const MatchState& state, const Vec2 from,
                    const TeamSide side, const std::vector<Remembered>& opponents,
                    const CarryCandidateRules& rules) {
  const CarryScoringConfig& scoring = rules.scoring;
  const Pitch& pitch = state.pitch();
  candidate.risk = touchRisk(candidate, from, opponents, pitch, rules);
  candidate.keep = 1.0 - candidate.risk;
  const Goal goal = attackedGoal(pitch, side);
  candidate.progression = std::clamp(
      (SimCore::distance(from, goal.center) - SimCore::distance(candidate.target, goal.center)) /
          pitch.lengthMeters(),
      -1.0, 1.0);
  const auto& grid = state.pitchControl();
  candidate.space =
      grid ? grid->controlAt(side, candidate.target) - grid->controlAt(side, from) : 0.0;
  candidate.shot =
      std::max(0.0, shotValue(candidate.target, goal, scoring) - shotValue(from, goal, scoring));
  candidate.bypassed = bypassedBy(state, from, candidate.target, side, opponents);
  candidate.utility = carryContributions(candidate, scoring).total();
  if (candidate.keep < scoring.minKeepChance) {
    candidate.rejection = CarryRejection::kUnlikely;
  }
}

// The touch's error factor for his ball control, and for a dribble his
// dribbling.
[[nodiscard]] double touchError(const PlayerAttributes& attributes, const bool dribble,
                                const CarryScoringConfig& scoring) noexcept {
  const double control = lerp(scoring.maxTouchError, scoring.minTouchError, attributes.ballControl);
  return dribble ? control *
                       lerp(scoring.maxDribbleError, scoring.minDribbleError, attributes.dribbling)
                 : control;
}

// The carry or turn along `direction`: a turn for a direction away from where
// he faces.
[[nodiscard]] CarryCandidate carryAlong(const MatchState& state, const std::size_t carrierIndex,
                                        const PlayerMatchState& carrier, const Vec2 from,
                                        const Vec2 direction,
                                        const std::vector<Remembered>& opponents,
                                        const CarryCandidateRules& rules) {
  const CarryScoringConfig& scoring = rules.scoring;
  const bool turn = direction.dot(state.players()[carrierIndex].facing) < 0.0;
  double distance = safeDistance(carrier, direction, opponents, state.pitch(), rules);
  if (turn) {
    distance = std::min(distance, scoring.turnDistance);
  }
  const PlayerMatchState& player = state.players()[carrierIndex];
  CarryCandidate candidate{
      .kind = turn ? CarryKind::kTurn : CarryKind::kCarry,
      .target = carrier.position + (direction * distance),
      .distance = distance,
      .errorFactor = touchError(carrier.attributes, false, scoring),
      .turnSeconds =
          turn ? scoring.turnSeconds + (std::max(0.0, player.velocity.dot(player.facing)) /
                                        player.attributes.acceleration)
               : 0.0};
  const double touch = std::clamp(carrier.attributes.maxSpeed * scoring.touchSeconds,
                                  scoring.minTouchDistance, scoring.maxTouchDistance);
  const double first = std::max(std::min(touch, distance), scoring.minTouchDistance);
  planTouch(candidate, carrier, from, carrier.position + (direction * first), 1.0, state.pitch(),
            rules);
  scoreCandidate(candidate, state, from, carrier.side, opponents, rules);
  const double shortest = turn ? scoring.minTouchDistance : scoring.minCarryDistance;
  if (distance < shortest) {
    candidate.rejection = CarryRejection::kTooShort;
  }
  return candidate;
}

// The dribble past `opponent` on the side `side` (+1 his left as the carrier
// sees him, -1 his right), or none if he stands too close to go past or the
// ball would leave the pitch.
[[nodiscard]] std::optional<CarryCandidate> dribblePast(const MatchState& state,
                                                        const PlayerMatchState& carrier,
                                                        const Vec2 from, const Remembered& opponent,
                                                        const double side,
                                                        const std::vector<Remembered>& opponents,
                                                        const CarryCandidateRules& rules) {
  const CarryScoringConfig& scoring = rules.scoring;
  const Vec2 offset = opponent.player.position - from;
  const double distance = offset.length();
  if (distance <= scoring.dribbleClearance) {
    return std::nullopt;
  }
  const Vec2 along = offset * (1.0 / distance);
  const Vec2 across = Vec2{.x = -along.y, .y = along.x} * side;
  // The ball's way from `from` to the point dribbleBeyond behind him passes
  // him at dribbleClearance: d · w / sqrt((d + b)² + w²) = c.
  const double reach = distance + scoring.dribbleBeyond;
  const double wide =
      scoring.dribbleClearance * reach /
      std::sqrt((distance * distance) - (scoring.dribbleClearance * scoring.dribbleClearance));
  const Vec2 target = opponent.player.position + (along * scoring.dribbleBeyond) + (across * wide);
  if (!state.pitch().contains(target)) {
    return std::nullopt;
  }
  CarryCandidate candidate{.kind = CarryKind::kDribble,
                           .target = target,
                           .distance = SimCore::distance(carrier.position, target),
                           .errorFactor = touchError(carrier.attributes, true, scoring),
                           .subject = opponent.player.playerId};
  planTouch(candidate, carrier, from, target,
            lerp(scoring.slowDribbleShare, scoring.quickDribbleShare, carrier.attributes.dribbling),
            state.pitch(), rules);
  scoreCandidate(candidate, state, from, carrier.side, opponents, rules);
  return candidate;
}

[[nodiscard]] bool isShare(const double value) noexcept {
  return value >= 0.0 && value <= 1.0;  // false for NaN
}

[[nodiscard]] bool isPositiveFinite(const double value) noexcept {
  return value > 0.0 && value <= std::numeric_limits<double>::max();
}

[[nodiscard]] bool isWeight(const double value) noexcept {
  return value >= 0.0 && value <= kMaxScoringWeight;
}

[[nodiscard]] bool isValid(const CarryPhaseFactors& factors) noexcept {
  return isWeight(factors.progression) && isWeight(factors.space) && isWeight(factors.shot) &&
         isWeight(factors.bypassed) && isWeight(factors.risk);
}

[[nodiscard]] const CarryPhaseFactors* factorsOf(const CarryScoringConfig& scoring,
                                                 const SimTactics::TacticalPhase phase) noexcept {
  switch (phase) {
    case SimTactics::TacticalPhase::kBuildUp:
      return &scoring.buildUp;
    case SimTactics::TacticalPhase::kProgression:
      return &scoring.progression;
    case SimTactics::TacticalPhase::kFinalThird:
      return &scoring.finalThird;
    case SimTactics::TacticalPhase::kAttackingTransition:
      return &scoring.attackingTransition;
    default:
      return nullptr;
  }
}

}  // namespace

void validate(const CarryScoringConfig& config) {
  constexpr double kMax = std::numeric_limits<double>::max();
  const bool valid =
      isShare(config.minConfidence) && isShare(config.minKeepChance) &&
      isShare(config.dribbleCone) && isShare(config.sameDirection) && isShare(config.touchShare) &&
      isPositiveFinite(config.horizonSeconds) && isPositiveFinite(config.minCarryDistance) &&
      isPositiveFinite(config.gapRadius) && config.pitchMargin >= 0.0 &&
      config.pitchMargin <= kMax && config.safetySeconds >= 0.0 && config.safetySeconds <= kMax &&
      isPositiveFinite(config.lossMarginSeconds) && isPositiveFinite(config.touchSeconds) &&
      config.lookBackSeconds >= 0.0 && config.lookBackSeconds <= kMax &&
      isPositiveFinite(config.minTouchDistance) &&
      config.maxTouchDistance >= config.minTouchDistance &&
      isPositiveFinite(config.maxTouchDistance) && isPositiveFinite(config.turnDistance) &&
      isPositiveFinite(config.dribbleRadius) && config.dribbleClearance >= 0.0 &&
      config.dribbleClearance <= kMax && isPositiveFinite(config.dribbleBeyond) &&
      isPositiveFinite(config.maxTouchError) && isPositiveFinite(config.minTouchError) &&
      isPositiveFinite(config.maxDribbleError) && isPositiveFinite(config.minDribbleError) &&
      isPositiveFinite(config.slowDribbleShare) && isPositiveFinite(config.quickDribbleShare) &&
      isPositiveFinite(config.shotDistance) && isPositiveFinite(config.fullOpening) &&
      isPositiveFinite(config.pressureRadius) && isWeight(config.keepWeight) &&
      isWeight(config.progressionWeight) && isWeight(config.spaceWeight) &&
      isWeight(config.shotWeight) && isWeight(config.bypassWeight) && isWeight(config.riskWeight) &&
      isWeight(config.holdWeight) && isWeight(config.turnCost) && config.turnSeconds >= 0.0 &&
      config.turnSeconds <= kMax && isValid(config.buildUp) && isValid(config.progression) &&
      isValid(config.finalThird) && isValid(config.attackingTransition);
  if (!valid) {
    throw std::invalid_argument("carry scoring: invalid configuration");
  }
}

CarryScoringConfig carryScoringForPhase(const CarryScoringConfig& scoring,
                                        const SimTactics::TacticalPhase phase) noexcept {
  const CarryPhaseFactors* factors = factorsOf(scoring, phase);
  if (factors == nullptr) {
    return scoring;
  }
  CarryScoringConfig adjusted = scoring;
  adjusted.progressionWeight = scoring.progressionWeight * factors->progression;
  adjusted.spaceWeight = scoring.spaceWeight * factors->space;
  adjusted.shotWeight = scoring.shotWeight * factors->shot;
  adjusted.bypassWeight = scoring.bypassWeight * factors->bypassed;
  adjusted.riskWeight = scoring.riskWeight * factors->risk;
  return adjusted;
}

CarryScoringConfig carryScoringForRisk(const CarryScoringConfig& scoring,
                                       const double passingRisk) noexcept {
  CarryScoringConfig adjusted = scoring;
  adjusted.progressionWeight = scoring.progressionWeight * (0.5 + passingRisk);
  adjusted.riskWeight = scoring.riskWeight * (1.5 - passingRisk);
  adjusted.minKeepChance = std::min(1.0, scoring.minKeepChance * (1.3 - (0.6 * passingRisk)));
  return adjusted;
}

std::string_view carryRejectionName(const CarryRejection rejection) noexcept {
  switch (rejection) {
    case CarryRejection::kValid:
      return "valid";
    case CarryRejection::kTooShort:
      return "too short";
    case CarryRejection::kUnlikely:
      return "unlikely to keep the ball";
  }
  return "unknown";
}

CarryContributions carryContributions(const CarryCandidate& candidate,
                                      const CarryScoringConfig& scoring) noexcept {
  if (candidate.kind == CarryKind::kHold) {
    return {.hold = scoring.holdWeight * candidate.keep};
  }
  return {.keep = scoring.keepWeight * candidate.keep,
          .progression = scoring.progressionWeight * candidate.progression,
          .space = scoring.spaceWeight * candidate.space,
          .shot = scoring.shotWeight * candidate.shot,
          .bypassed = scoring.bypassWeight * static_cast<double>(candidate.bypassed),
          .risk = -(scoring.riskWeight * candidate.risk),
          .turn = -(scoring.turnCost * candidate.turnSeconds)};
}

std::string_view dominantContribution(const CarryContributions& parts) noexcept {
  const std::array<std::pair<std::string_view, double>, 8> named{{
      {"keep", parts.keep},
      {"progression", parts.progression},
      {"space", parts.space},
      {"shot", parts.shot},
      {"bypassed", parts.bypassed},
      {"risk", parts.risk},
      {"turn", parts.turn},
      {"hold", parts.hold},
  }};
  std::size_t dominant = 0;
  for (std::size_t index = 1; index < named.size(); ++index) {
    if (std::abs(named.at(index).second) > std::abs(named.at(dominant).second)) {
      dominant = index;
    }
  }
  return named.at(dominant).first;
}

std::vector<CarryCandidate> generateCarryCandidates(const MatchState& state,
                                                    const std::size_t carrierIndex,
                                                    const SimCore::SimTick now,
                                                    const double secondsPerTick,
                                                    const CarryCandidateRules& rules) {
  if (!rules.scoring.enabled || state.ball().held) {
    return {};
  }
  const CarryScoringConfig& scoring = rules.scoring;
  const PlayerMatchState carrier = carrierRunning(state, carrierIndex, rules.ball);
  const Vec2 from = state.ball().position;
  const auto opponents = LaneReach::rememberedPlayers(state, carrierIndex, now, secondsPerTick,
                                                      rules.perception, scoring.minConfidence)
                             .opponents;

  std::vector<CarryCandidate> candidates;
  // Holding the ball: under as much pressure as the nearest remembered
  // opponent puts on him.
  double nearest = std::numeric_limits<double>::infinity();
  for (const Remembered& opponent : opponents) {
    nearest = std::min(nearest, SimCore::distance(opponent.player.position, carrier.position));
  }
  CarryCandidate hold{.kind = CarryKind::kHold, .target = carrier.position};
  hold.risk = std::max(0.0, 1.0 - (nearest / scoring.pressureRadius));
  hold.keep = 1.0 - hold.risk;
  hold.utility = carryContributions(hold, scoring).total();
  candidates.push_back(hold);

  // The directions he carries the ball in, the first of nearly equal ones.
  const Vec2 forward{.x = attackingDirection(carrier.side), .y = 0.0};
  std::vector<Vec2> directions{
      SimCore::unitOr(attackedGoal(state.pitch(), carrier.side).center - from, forward)};
  for (const Vec2 gap : gapDirections(carrier.position, opponents, scoring.gapRadius)) {
    directions.push_back(gap);
  }
  if (const auto nearestOpponent = std::ranges::min_element(
          opponents, {},
          [&](const Remembered& opponent) {
            return (opponent.player.position - carrier.position).lengthSquared();
          });
      nearestOpponent != opponents.end()) {
    directions.push_back(
        SimCore::unitOr(carrier.position - nearestOpponent->player.position, forward));
  }
  if (const auto space = spaceDirection(state, carrier.position, carrier.side,
                                        carrier.attributes.maxSpeed * scoring.horizonSeconds,
                                        scoring.minCarryDistance)) {
    directions.push_back(*space);
  }
  std::vector<Vec2> distinct;
  for (const Vec2 direction : directions) {
    if (std::ranges::none_of(distinct, [&](const Vec2 kept) {
          return kept.dot(direction) >= scoring.sameDirection;
        })) {
      distinct.push_back(direction);
    }
  }
  for (const Vec2 direction : distinct) {
    candidates.push_back(
        carryAlong(state, carrierIndex, carrier, from, direction, opponents, rules));
  }

  if (!isGoalkeeper(state, carrierIndex)) {
    for (const Remembered& opponent : opponents) {
      const Vec2 offset = opponent.player.position - from;
      const double distance = offset.length();
      if (distance > scoring.dribbleRadius || distance == 0.0 ||
          offset.dot(forward) < scoring.dribbleCone * distance) {
        continue;
      }
      for (const double side : {1.0, -1.0}) {
        if (auto candidate = dribblePast(state, carrier, from, opponent, side, opponents, rules)) {
          candidates.push_back(*candidate);
        }
      }
    }
  }

  orderCarryCandidates(candidates);
  return candidates;
}

void orderCarryCandidates(std::vector<CarryCandidate>& candidates) {
  std::ranges::stable_sort(candidates, [](const CarryCandidate& left, const CarryCandidate& right) {
    return std::tuple(!left.isValid(), -left.utility, left.kind,
                      left.subject.value_or(SimCore::PlayerId::invalid()).value()) <
           std::tuple(!right.isValid(), -right.utility, right.kind,
                      right.subject.value_or(SimCore::PlayerId::invalid()).value());
  });
}

const CarryRun* activeCarry(const MatchState& state, const std::size_t playerIndex) {
  const auto& run = state.tactical(playerIndex).carry;
  const BallState& ball = state.ball();
  const SimCore::PlayerId player = state.players()[playerIndex].playerId;
  if (!run || ball.owner != player || !ball.lastTouch || ball.lastTouch->playerId != player ||
      !run->touchedAt || ball.lastTouch->tick != *run->touchedAt) {
    return nullptr;
  }
  return &*run;
}

}  // namespace ElyverseFootball::SimMatch
