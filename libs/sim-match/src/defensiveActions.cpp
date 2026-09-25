#include "defensiveActions.hpp"

#include <algorithm>
#include <limits>
#include <optional>
#include <stdexcept>

#include "actionGeometry.hpp"
#include "responsibility.hpp"
#include "teamFrame.hpp"

namespace ElyverseFootball::SimMatch {
namespace {

using ActionGeometry::directionTo;
using ActionGeometry::distanceBetween;
using SimCore::Vec2;
using SimTactics::Responsibility;

// An opponent this close to his own goal line is his goalkeeper, whom nobody
// marks.
constexpr double kKeeperDepth = 8.0;  // m

// The distance from the ball beyond which an opponent adds no threat.
constexpr double kThreatDistance = 30.0;  // m

// A teammate this close to the ball is under way to it, and worth covering.
constexpr double kCoverRange = 15.0;  // m

class DefensiveScorer {
 public:
  DefensiveScorer(const MatchState& state, const std::size_t playerIndex,
                  const DesiredRegion& region, const DefensiveRules& rules)
      : state_(&state),
        playerIndex_(playerIndex),
        region_(&region),
        rules_(&rules),
        remembered_(rememberedPlayers(state, playerIndex, rules.now, rules.secondsPerTick,
                                      *rules.perception, rules.positioning->minConfidence)) {}

  [[nodiscard]] const PlayerMatchState& player() const { return state_->players()[playerIndex_]; }

  [[nodiscard]] const std::vector<RememberedPlayer>& remembered() const noexcept {
    return remembered_;
  }

  [[nodiscard]] double weight(const Responsibility responsibility) const {
    return ActionGeometry::tacticOf(*state_, player().side)
        .responsibilityWeight(slotIndex(*state_, playerIndex_), responsibility);
  }

  // The centre of the own goal.
  [[nodiscard]] Vec2 ownGoal() const {
    const Pitch& pitch = state_->pitch();
    return {.x = xAtDepth(player().side, 0.0, pitch), .y = pitch.widthMeters() / 2.0};
  }

  // A point this far from `from`, toward the own goal: goal-side of it.
  [[nodiscard]] Vec2 goalSideOf(const Vec2 from, const double distance) const {
    return state_->pitch().clamp(from + (directionTo(from, ownGoal()) * distance));
  }

  // How dangerous an opponent is, in [0, 1]: half for how close he is to the
  // own goal, half for how close to the ball.
  [[nodiscard]] double threat(const Vec2 opponent) const {
    const Pitch& pitch = state_->pitch();
    const double nearGoal = 1.0 - (depthOf(player().side, opponent, pitch) / pitch.lengthMeters());
    const double nearBall =
        1.0 - std::min(1.0, distanceBetween(opponent, state_->ball().position) / kThreatDistance);
    return (0.5 * nearGoal) + (0.5 * nearBall);
  }

  // Whether an opponent is his side's goalkeeper, by where he stands.
  [[nodiscard]] bool isKeeper(const RememberedPlayer& opponent) const {
    return depthOf(opponentOf(player().side), opponent.position, state_->pitch()) < kKeeperDepth;
  }

  // An opponent's speed toward the own goal, 0 if he runs away from it.
  [[nodiscard]] double runSpeed(const RememberedPlayer& opponent) const {
    return std::max(0.0, opponent.velocity.dot(directionTo(opponent.position, ownGoal())));
  }

  [[nodiscard]] ActionCandidate score(const ActionType type, const Vec2 target,
                                      const double responsibility, const double urgency,
                                      const std::optional<SimCore::PlayerId> subject) const {
    const DefensiveConfig& config = *rules_->config;
    const double penalty = ActionGeometry::regionPenalty(*state_, playerIndex_, target, *region_,
                                                         {.now = rules_->now,
                                                          .secondsPerTick = rules_->secondsPerTick,
                                                          .positioning = rules_->positioning,
                                                          .perception = rules_->perception});
    const auto& grid = state_->pitchControl();
    const double dangerousSpace = grid ? grid->controlAt(opponentOf(player().side), target) : 0.5;
    const ActionScores scores{
        .responsibility = config.responsibilityWeight * responsibility,
        .region = -config.regionWeight * penalty,
        .space = config.spaceWeight * dangerousSpace,
        .lane = 0.0,
        .urgency = config.urgencyWeight * urgency,
        .effort = -config.effortWeight *
                  (distanceBetween(player().position, target) / config.effortScale)};
    return {.type = type,
            .target = target,
            .subject = subject,
            .scores = scores,
            .utility = scores.total()};
  }

 private:
  const MatchState* state_;
  std::size_t playerIndex_;
  const DesiredRegion* region_;
  const DefensiveRules* rules_;
  std::vector<RememberedPlayer> remembered_;
};

}  // namespace

void validate(const DefensiveConfig& config) {
  constexpr double kMax = std::numeric_limits<double>::max();
  const auto positive = [](const double value) { return value > 0.0 && value <= kMax; };
  const auto weight = [](const double value) { return value >= 0.0 && value <= kMax; };
  const bool valid = positive(config.markRadius) && positive(config.markDistance) &&
                     positive(config.trackRadius) && positive(config.runnerSpeed) &&
                     weight(config.trackLeadSeconds) && positive(config.coverDistance) &&
                     positive(config.effortScale) && config.holdResponsibility >= 0.0 &&
                     config.holdResponsibility <= 1.0 && positive(config.temperature) &&
                     weight(config.responsibilityWeight) && weight(config.regionWeight) &&
                     weight(config.spaceWeight) && weight(config.urgencyWeight) &&
                     weight(config.effortWeight);
  if (!valid) {
    throw std::invalid_argument("defensive decisions: invalid configuration");
  }
}

std::vector<ActionCandidate> generateDefensiveCandidates(const MatchState& state,
                                                         const std::size_t playerIndex,
                                                         const DesiredRegion& region,
                                                         const DefensiveRules& rules) {
  const DefensiveConfig& config = *rules.config;
  const DefensiveScorer scorer(state, playerIndex, region, rules);
  const PlayerMatchState& player = state.players()[playerIndex];
  std::vector<ActionCandidate> candidates;
  candidates.push_back(scorer.score(
      ActionType::kHoldPosition, region.center,
      std::max(config.holdResponsibility, scorer.weight(Responsibility::kHoldDefensiveLine)), 0.0,
      std::nullopt));

  // The nearest opponent in his zone, and the fastest runner near him.
  const RememberedPlayer* marked = nullptr;
  const RememberedPlayer* runner = nullptr;
  double markedDistance = config.markRadius;
  double runnerSpeed = config.runnerSpeed;
  const RememberedPlayer* covered = nullptr;
  double coveredDistance = std::numeric_limits<double>::infinity();
  for (const RememberedPlayer& other : scorer.remembered()) {
    if (other.teammate) {
      const double toBall = distanceBetween(other.position, state.ball().position);
      if (toBall < coveredDistance) {
        covered = &other;
        coveredDistance = toBall;
      }
      continue;
    }
    if (scorer.isKeeper(other)) {
      continue;
    }
    if (const double distance = distanceBetween(other.position, region.center);
        distance <= markedDistance) {
      marked = &other;
      markedDistance = distance;
    }
    const bool near = distanceBetween(other.position, player.position) <= config.trackRadius;
    if (const double speed = scorer.runSpeed(other); near && speed >= runnerSpeed) {
      runner = &other;
      runnerSpeed = speed;
    }
  }

  if (marked != nullptr) {
    candidates.push_back(scorer.score(ActionType::kMarkOpponent,
                                      scorer.goalSideOf(marked->position, config.markDistance),
                                      scorer.weight(Responsibility::kMarkOpponent),
                                      scorer.threat(marked->position), marked->playerId));
  }
  if (runner != nullptr) {
    const Vec2 ahead = runner->position + (runner->velocity * config.trackLeadSeconds);
    candidates.push_back(
        scorer.score(ActionType::kTrackRunner, scorer.goalSideOf(ahead, config.markDistance),
                     std::max(scorer.weight(Responsibility::kMarkOpponent),
                              scorer.weight(Responsibility::kCover)),
                     std::min(1.0, runnerSpeed / player.attributes.maxSpeed), runner->playerId));
  }
  if (covered != nullptr) {
    candidates.push_back(
        scorer.score(ActionType::kCover, scorer.goalSideOf(covered->position, config.coverDistance),
                     scorer.weight(Responsibility::kCover),
                     1.0 - std::min(1.0, coveredDistance / kCoverRange), covered->playerId));
  }
  return candidates;
}

}  // namespace ElyverseFootball::SimMatch
