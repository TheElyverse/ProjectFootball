#include "passDecision.hpp"

#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#include "passCandidates.hpp"
#include "stableMath.hpp"

namespace ElyverseFootball::SimMatch {
namespace {

void validate(const DecisionConfig& config) {
  constexpr double kMax = std::numeric_limits<double>::max();
  const bool valid = config.intervalTicks >= 1 && config.minHoldSeconds >= 0.0 &&
                     config.minHoldSeconds <= kMax && config.temperature > 0.0 &&
                     config.temperature <= kMax;
  if (!valid) {
    throw std::invalid_argument("pass decision: invalid configuration");
  }
  validate(config.scoring);
}

// How long the owner has had the ball: since his last touch, which is when
// he took it. An owner without a touch had the ball from the start.
[[nodiscard]] double heldSeconds(const BallState& ball, const SimCore::SimTick now,
                                 const double secondsPerTick) noexcept {
  if (!ball.lastTouch || ball.lastTouch->playerId != ball.owner) {
    return std::numeric_limits<double>::infinity();
  }
  return static_cast<double>(now.value() - ball.lastTouch->tick.value()) * secondsPerTick;
}

}  // namespace

std::optional<std::size_t> choosePass(const std::span<const PassCandidate> candidates,
                                      const double temperature,
                                      SimCore::RandomNumberGenerator& random) {
  // Valid candidates come first, the best of them at the front.
  std::size_t valid = 0;
  while (valid < candidates.size() && candidates[valid].isValid()) {
    ++valid;
  }
  if (valid == 0) {
    return std::nullopt;
  }

  // Weights relative to the best utility, so the largest is exactly 1 and no
  // weight overflows.
  const double best = candidates.front().utility;
  double total = 0.0;
  for (std::size_t index = 0; index < valid; ++index) {
    total += SimCore::stableExp((candidates[index].utility - best) / temperature);
  }
  const double draw = random.nextUniform() * total;
  double cumulative = 0.0;
  for (std::size_t index = 0; index < valid; ++index) {
    cumulative += SimCore::stableExp((candidates[index].utility - best) / temperature);
    if (draw < cumulative) {
      return index;
    }
  }
  // Only reachable through rounding of the sum: the last valid candidate.
  return valid - 1;
}

MatchSystem makePassDecisionSystem(const DecisionConfig& config, const PassCandidateRules& rules) {
  validate(config);
  PassCandidateRules scored = rules;
  scored.scoring = config.scoring;
  return {.name = std::string(kPassDecisionSystemName),
          .update =
              [config, scored](const MatchStepContext& context, const MatchState& current,
                               MatchStateWriter& next) {
                const BallState& ball = current.ball();
                if (!ball.owner || current.pendingPass()) {
                  return;
                }
                if (heldSeconds(ball, context.tick(), context.secondsPerTick()) <
                    config.minHoldSeconds) {
                  return;
                }
                const auto carrier = findPlayerIndex(current, *ball.owner);
                if (!carrier) {
                  return;
                }
                const std::vector<PassCandidate> candidates = generatePassCandidates(
                    current, *carrier, context.tick(), context.secondsPerTick(), scored);
                const auto chosen =
                    choosePass(candidates, config.temperature,
                               context.random(SimCore::RandomNumberGeneratorDomain::kAi));
                if (!chosen) {
                  return;
                }
                const PassCandidate& pass = candidates[*chosen];
                next.setPendingPass(PassIntent{.passer = *ball.owner,
                                               .target = pass.target,
                                               .speed = pass.speed,
                                               .receiver = pass.receiver});
              },
          .intervalTicks = config.intervalTicks,
          .phaseTicks = 0};
}

}  // namespace ElyverseFootball::SimMatch
