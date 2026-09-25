#include "passCandidates.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <optional>
#include <stdexcept>
#include <tuple>
#include <utility>

#include "spatialQueries.hpp"

namespace ElyverseFootball::SimMatch {
namespace {

using SimCore::Vec2;

// Spacing of the points along a pass where interception is checked.
constexpr double kLaneStep = 0.5;  // m

// A smooth step from 1 at value <= -1 through 1/2 at 0 to 0 at value >= 1:
// the cubic smoothstep, built from arithmetic only, so no std::exp and none
// of its platform differences enter the scores.
[[nodiscard]] double fallingStep(const double value) noexcept {
  const double rising = std::clamp((1.0 - value) / 2.0, 0.0, 1.0);
  return rising * rising * (3.0 - (2.0 * rising));
}

// Seconds a ball kicked at `speed` needs to roll `distance` meters, from
// s = v·t − a·t²/2; infinite if it stops before.
[[nodiscard]] double ballSeconds(const double distance, const double speed,
                                 const BallPhysics& physics) noexcept {
  const double deceleration = physics.rollingDeceleration;
  const double remaining = (speed * speed) - (2.0 * deceleration * distance);
  if (remaining < 0.0) {
    return std::numeric_limits<double>::infinity();
  }
  return (speed - std::sqrt(remaining)) / deceleration;
}

// An observed player as the carrier imagines him: where and how fast he
// believes he is, with default movement limits -- the carrier does not know
// anyone's attributes.
[[nodiscard]] PlayerMatchState imagined(const PlayerMatchState& actual, const Vec2 position,
                                        const Vec2 velocity) {
  PlayerMatchState player = actual;
  player.position = position;
  player.velocity = velocity;
  player.attributes = PlayerAttributes{};
  player.target = std::nullopt;
  return player;
}

// A remembered player, as the carrier imagines him now.
struct Remembered {
  PlayerMatchState player;
  double confidence = 0.0;
};

// The straight line a pass rolls along, up to where the receiver takes it.
struct Lane {
  Vec2 from;
  Vec2 direction;
  double speed = 0.0;
  // How far along the line the ball gets before the receiver controls it.
  double length = 0.0;
};

// Seconds the player needs to come within the control radius of a point:
// his arrival time less the radius covered at full speed. Empty if the
// point is off the pitch.
[[nodiscard]] std::optional<double> reachSeconds(const PlayerMatchState& player, const Vec2 point,
                                                 const Pitch& pitch,
                                                 const PassCandidateRules& rules) {
  const auto arrival = estimateArrivalSeconds(player, point, pitch);
  if (!arrival) {
    return std::nullopt;
  }
  return *arrival - (rules.reception.controlRadius / player.attributes.maxSpeed);
}

// How far the ball rolls before the receiver can take it: the first point
// on the line he reaches no later than the ball, or the whole distance.
[[nodiscard]] double receptionDistance(const PlayerMatchState& receiver, const Lane& lane,
                                       const double distance, const Pitch& pitch,
                                       const PassCandidateRules& rules) {
  for (double along = kLaneStep; along < distance; along += kLaneStep) {
    const auto reach = reachSeconds(receiver, lane.from + (lane.direction * along), pitch, rules);
    if (reach && *reach <= ballSeconds(along, lane.speed, rules.ball)) {
      return along;
    }
  }
  return distance;
}

// The risk that this opponent reaches the pass before the receiver does. His
// margin is the smallest time he has to spare at any point of the lane --
// negative if he gets there before the ball.
[[nodiscard]] double riskFrom(const Remembered& opponent, const Lane& lane, const Pitch& pitch,
                              const PassCandidateRules& rules) {
  double margin = std::numeric_limits<double>::infinity();
  for (double along = kLaneStep; along < lane.length; along += kLaneStep) {
    const auto reach =
        reachSeconds(opponent.player, lane.from + (lane.direction * along), pitch, rules);
    if (reach) {
      margin = std::min(margin, *reach - ballSeconds(along, lane.speed, rules.ball));
    }
  }
  if (margin == std::numeric_limits<double>::infinity()) {
    return 0.0;
  }
  return opponent.confidence * fallingStep(margin / rules.scoring.interceptionMarginSeconds);
}

[[nodiscard]] PassRejection rejectionOf(const PassCandidate& candidate,
                                        const PassCandidateRules& rules) noexcept {
  if (candidate.distance < rules.scoring.minPassDistance) {
    return PassRejection::kTooClose;
  }
  if (candidate.distance > rules.scoring.maxPassDistance) {
    return PassRejection::kTooFar;
  }
  if (passReach(candidate.speed, rules.ball) < candidate.distance) {
    return PassRejection::kOutOfReach;
  }
  if (candidate.completion < rules.scoring.minCompletion) {
    return PassRejection::kUnlikely;
  }
  return PassRejection::kValid;
}

}  // namespace

std::string_view passRejectionName(const PassRejection rejection) noexcept {
  switch (rejection) {
    case PassRejection::kValid:
      return "valid";
    case PassRejection::kTooClose:
      return "too close";
    case PassRejection::kTooFar:
      return "too far";
    case PassRejection::kOutOfReach:
      return "out of reach";
    case PassRejection::kUnlikely:
      return "unlikely to arrive";
  }
  return "unknown";
}

PassContributions passContributions(const PassCandidate& candidate,
                                    const PassScoringConfig& scoring) noexcept {
  return {.completion = scoring.completionWeight * candidate.completion,
          .progression = scoring.progressionWeight * candidate.progression,
          .pressure = -(scoring.pressureWeight * candidate.receiverPressure),
          .risk = -(scoring.riskWeight * candidate.interceptionRisk)};
}

std::string_view dominantContribution(const PassContributions& parts) noexcept {
  const std::array<std::pair<std::string_view, double>, 4> named{{
      {"completion", parts.completion},
      {"progression", parts.progression},
      {"pressure", parts.pressure},
      {"risk", parts.risk},
  }};
  std::size_t dominant = 0;
  for (std::size_t index = 1; index < named.size(); ++index) {
    if (std::abs(named.at(index).second) > std::abs(named.at(dominant).second)) {
      dominant = index;
    }
  }
  return named.at(dominant).first;
}

void validate(const PassScoringConfig& config) {
  const auto finiteIn = [](const double value, const double min, const double max) {
    return value >= min && value <= max;  // false for NaN
  };
  constexpr double kMax = std::numeric_limits<double>::max();
  const bool valid =
      finiteIn(config.minConfidence, 0.0, 1.0) && finiteIn(config.minCompletion, 0.0, 1.0) &&
      finiteIn(config.minPassDistance, 0.0, kMax) && finiteIn(config.maxPassDistance, 0.0, kMax) &&
      config.maxPassDistance > config.minPassDistance &&
      finiteIn(config.interceptionMarginSeconds, 0.0, kMax) &&
      config.interceptionMarginSeconds > 0.0 && finiteIn(config.pressureRadius, 0.0, kMax) &&
      config.pressureRadius > 0.0 && finiteIn(config.completionWeight, 0.0, kMaxScoringWeight) &&
      finiteIn(config.progressionWeight, 0.0, kMaxScoringWeight) &&
      finiteIn(config.pressureWeight, 0.0, kMaxScoringWeight) &&
      finiteIn(config.riskWeight, 0.0, kMaxScoringWeight);
  if (!valid) {
    throw std::invalid_argument("pass scoring: invalid configuration");
  }
}

double attackingDirection(const TeamSide side) noexcept {
  return side == TeamSide::kHome ? 1.0 : -1.0;
}

std::vector<PassCandidate> generatePassCandidates(const MatchState& state,
                                                  const std::size_t carrierIndex,
                                                  const SimCore::SimTick now,
                                                  const double secondsPerTick,
                                                  const PassCandidateRules& rules) {
  const PlayerMatchState& carrier = state.players()[carrierIndex];
  const PlayerPerception& memory = state.perception(carrierIndex);
  const Vec2 from = state.ball().position;

  // Everyone the carrier remembers well enough, split into teammates and
  // opponents, at the positions he believes they have now.
  std::vector<Remembered> teammates;
  std::vector<Remembered> opponents;
  for (const Observation& observation : memory.observations) {
    if (observation.entity.isBall() || observation.confidence < rules.scoring.minConfidence) {
      continue;
    }
    const auto index = findPlayerIndex(state, observation.entity.playerId());
    if (!index) {
      continue;
    }
    const PlayerMatchState& actual = state.players()[*index];
    const Remembered remembered{
        .player =
            imagined(actual, estimatePosition(observation, now, secondsPerTick, rules.perception),
                     observation.velocity),
        .confidence = observation.confidence};
    (actual.side == carrier.side ? teammates : opponents).push_back(remembered);
  }

  std::vector<PassCandidate> candidates;
  candidates.reserve(teammates.size());
  for (const Remembered& receiver : teammates) {
    PassCandidate candidate{.receiver = receiver.player.playerId,
                            .target = receiver.player.position,
                            .receiverConfidence = receiver.confidence};
    const Vec2 offset = candidate.target - from;
    candidate.distance = std::sqrt(offset.lengthSquared());
    candidate.speed = planPassSpeed(candidate.distance, rules.ball, rules.passing);
    Lane lane{.from = from,
              .direction =
                  candidate.distance > 0.0 ? offset * (1.0 / candidate.distance) : carrier.facing,
              .speed = candidate.speed,
              .length = 0.0};
    lane.length =
        receptionDistance(receiver.player, lane, candidate.distance, state.pitch(), rules);

    double arrives = 1.0;
    double nearestOpponent = std::numeric_limits<double>::infinity();
    for (const Remembered& opponent : opponents) {
      arrives *= 1.0 - riskFrom(opponent, lane, state.pitch(), rules);
      nearestOpponent =
          std::min(nearestOpponent,
                   std::sqrt((opponent.player.position - candidate.target).lengthSquared()));
    }
    candidate.interceptionRisk = 1.0 - arrives;
    candidate.completion = arrives * candidate.receiverConfidence;
    candidate.progression = std::clamp(
        attackingDirection(carrier.side) * offset.x / state.pitch().lengthMeters(), -1.0, 1.0);
    candidate.receiverPressure =
        std::max(0.0, 1.0 - (nearestOpponent / rules.scoring.pressureRadius));

    candidate.utility = passContributions(candidate, rules.scoring).total();
    candidate.rejection = rejectionOf(candidate, rules);
    candidates.push_back(candidate);
  }

  std::ranges::sort(candidates, [](const PassCandidate& left, const PassCandidate& right) {
    return std::tuple(!left.isValid(), -left.utility, left.receiver) <
           std::tuple(!right.isValid(), -right.utility, right.receiver);
  });
  return candidates;
}

}  // namespace ElyverseFootball::SimMatch
