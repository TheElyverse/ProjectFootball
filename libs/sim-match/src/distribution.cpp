#include "distribution.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

#include "ballPhysics.hpp"
#include "configChecks.hpp"
#include "laneReach.hpp"

namespace ElyverseFootball::SimMatch {
namespace {

using ConfigChecks::isFiniteNonNegative;
using ConfigChecks::isPositiveFinite;
using ConfigChecks::isWithin;
using LaneReach::Remembered;
using SimCore::Vec2;

[[nodiscard]] PassRejection rejectionOf(const PassCandidate& candidate,
                                        const PassCandidateRules& rules,
                                        const DistributionConfig& config) noexcept {
  if (candidate.distance < config.minLongDistance) {
    return PassRejection::kTooClose;
  }
  if (!(candidate.speed > 0.0) || candidate.speed > rules.passing.maxLoftedSpeed) {
    return PassRejection::kOutOfReach;
  }
  if (candidate.completion < rules.scoring.minCompletion) {
    return PassRejection::kUnlikely;
  }
  return PassRejection::kValid;
}

}  // namespace

void validate(const DistributionConfig& config) {
  const bool valid =
      isFiniteNonNegative(config.holdSeconds) && isFiniteNonNegative(config.clearSeconds) &&
      isPositiveFinite(config.throwRange) && isFiniteNonNegative(config.minLongDistance) &&
      isPositiveFinite(config.flightSeconds) && isPositiveFinite(config.contestMarginSeconds) &&
      isWithin(config.directnessWeight, 0.0, kMaxScoringWeight);
  if (!valid) {
    throw std::invalid_argument("distribution: invalid configuration");
  }
}

double directnessBias(const double directness, const DistributionConfig& config) noexcept {
  return config.directnessWeight * ((2.0 * directness) - 1.0);
}

std::vector<PassCandidate> generateLongBallCandidates(const MatchState& state,
                                                      const std::size_t carrierIndex,
                                                      const SimCore::StepTime time,
                                                      const PassCandidateRules& rules,
                                                      const DistributionConfig& config) {
  const PlayerMatchState& carrier = state.players()[carrierIndex];
  const Vec2 from = state.ball().position;
  const Pitch& pitch = state.pitch();
  const double radius = rules.reception.controlRadius;
  constexpr double kNever = std::numeric_limits<double>::infinity();

  const auto [teammates, opponents] = LaneReach::rememberedPlayers(
      state, carrierIndex, time, rules.perception, rules.scoring.minConfidence);

  std::vector<PassCandidate> candidates;
  candidates.reserve(teammates.size());
  for (const Remembered& receiver : teammates) {
    PassCandidate candidate{.receiver = receiver.player.playerId,
                            .target = receiver.player.position,
                            .receiverConfidence = receiver.confidence,
                            .lofted = true};
    candidate.distance = std::sqrt((candidate.target - from).lengthSquared());
    candidate.speed =
        flightSpeed(candidate.distance, config.flightSeconds, rules.ball).value_or(0.0);

    // Nobody wins the ball before it comes down.
    const double flight = config.flightSeconds;
    const double receiverSeconds = std::max(
        LaneReach::reachSeconds(receiver.player, candidate.target, pitch, radius).value_or(kNever),
        flight);
    double arrives = 1.0;
    double nearestOpponent = kNever;
    for (const Remembered& opponent : opponents) {
      if (const auto reach =
              LaneReach::reachSeconds(opponent.player, candidate.target, pitch, radius)) {
        const double margin = std::max(*reach, flight) - receiverSeconds;
        arrives *=
            1.0 - LaneReach::riskOfMargin(margin, opponent.confidence, config.contestMarginSeconds);
      }
      nearestOpponent =
          std::min(nearestOpponent,
                   std::sqrt((opponent.player.position - candidate.target).lengthSquared()));
    }
    scorePassCandidate(candidate, from, carrier.side, arrives, nearestOpponent, pitch,
                       rules.scoring);
    candidate.rejection = rejectionOf(candidate, rules, config);
    candidates.push_back(candidate);
  }

  orderPassCandidates(candidates);
  return candidates;
}

}  // namespace ElyverseFootball::SimMatch
