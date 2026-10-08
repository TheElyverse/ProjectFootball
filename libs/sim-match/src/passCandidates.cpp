#include "passCandidates.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <tuple>
#include <utility>

#include "laneReach.hpp"
#include "spatialQueries.hpp"

namespace ElyverseFootball::SimMatch {
namespace {

using SimCore::Vec2;

using LaneReach::ballSeconds;
using LaneReach::kLaneStep;
using LaneReach::Lane;
using LaneReach::Remembered;

// How far the ball rolls before the receiver can take it: the first point
// on the line he reaches no later than the ball, or the whole distance.
[[nodiscard]] double receptionDistance(const PlayerMatchState& receiver, const Lane& lane,
                                       const double distance, const Pitch& pitch,
                                       const PassCandidateRules& rules) {
  // The samples are the running sum of kLaneStep, not step * kLaneStep: IEEE
  // addition is deterministic on every platform, and the exact sample points
  // are what the pinned scenario results were recorded with.
  // NOLINTNEXTLINE(bugprone-float-loop-counter)
  for (double along = kLaneStep; along < distance; along += kLaneStep) {
    const auto reach = LaneReach::reachSeconds(receiver, lane.from + (lane.direction * along),
                                               pitch, rules.reception.controlRadius);
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
  const double margin =
      LaneReach::laneMargin(lane, rules.ball, [&](const Vec2 point, double /*along*/) {
        return LaneReach::reachSeconds(opponent.player, point, pitch,
                                       rules.reception.controlRadius);
      });
  return LaneReach::riskOfMargin(margin, opponent.confidence,
                                 rules.scoring.interceptionMarginSeconds);
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
          .risk = -(scoring.riskWeight * candidate.interceptionRisk),
          .directness = candidate.lofted ? scoring.loftedBias : 0.0};
}

std::string_view dominantContribution(const PassContributions& parts) noexcept {
  const std::array<std::pair<std::string_view, double>, 5> named{{
      {"completion", parts.completion},
      {"progression", parts.progression},
      {"pressure", parts.pressure},
      {"risk", parts.risk},
      {"directness", parts.directness},
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
      finiteIn(config.riskWeight, 0.0, kMaxScoringWeight) &&
      finiteIn(config.loftedBias, -kMaxScoringWeight, kMaxScoringWeight);
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
  const Vec2 from = state.ball().position;

  // Everyone the carrier remembers well enough, at the positions he believes
  // they have now.
  const auto [teammates, opponents] = LaneReach::rememberedPlayers(
      state, carrierIndex, now, secondsPerTick, rules.perception, rules.scoring.minConfidence);

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
    scorePassCandidate(candidate, from, carrier.side, arrives, nearestOpponent, state.pitch(),
                       rules.scoring);
    candidate.rejection = rejectionOf(candidate, rules);
    candidates.push_back(candidate);
  }

  orderPassCandidates(candidates);
  return candidates;
}

// NOLINTBEGIN(bugprone-easily-swappable-parameters) -- a chance and a distance, as documented.
void scorePassCandidate(PassCandidate& candidate, const Vec2 from, const TeamSide side,
                        const double arrives, const double nearestOpponent, const Pitch& pitch,
                        const PassScoringConfig& scoring) noexcept {
  // NOLINTEND(bugprone-easily-swappable-parameters)
  const Vec2 offset = candidate.target - from;
  candidate.interceptionRisk = 1.0 - arrives;
  candidate.completion = arrives * candidate.receiverConfidence;
  candidate.progression =
      std::clamp(attackingDirection(side) * offset.x / pitch.lengthMeters(), -1.0, 1.0);
  candidate.receiverPressure = std::max(0.0, 1.0 - (nearestOpponent / scoring.pressureRadius));
  candidate.utility = passContributions(candidate, scoring).total();
}

void orderPassCandidates(std::vector<PassCandidate>& candidates) {
  std::ranges::sort(candidates, [](const PassCandidate& left, const PassCandidate& right) {
    return std::tuple(!left.isValid(), -left.utility, left.receiver, left.lofted) <
           std::tuple(!right.isValid(), -right.utility, right.receiver, right.lofted);
  });
}

}  // namespace ElyverseFootball::SimMatch
