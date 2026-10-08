#include "passDecision.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#include "choicePolicy.hpp"
#include "distribution.hpp"
#include "passCandidates.hpp"
#include "zones.hpp"

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
  validate(config.shooting);
  validate(config.distribution);
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

// How long the owner keeps a ball he has taken before he plays it: a ball in
// his hands at least the distribution's holdSeconds.
[[nodiscard]] double holdSeconds(const BallState& ball, const DecisionConfig& config) noexcept {
  return ball.held ? std::max(config.minHoldSeconds, config.distribution.holdSeconds)
                   : config.minHoldSeconds;
}

// The carrier's passes, ordered by orderPassCandidates(): his ground passes
// -- from his hands throws, no farther than throwRange -- and, a goalkeeper,
// his long balls, with his tactic's directness as the lofted bias of
// `rules`, which they are scored with.
[[nodiscard]] std::vector<PassCandidate> passOptions(const MatchStepContext& context,
                                                     const MatchState& current,
                                                     const std::size_t carrier,
                                                     const DistributionConfig& distribution,
                                                     PassCandidateRules& rules) {
  const bool keeper = isGoalkeeper(current, carrier);
  const auto& tactic = current.tactics().of(current.players()[carrier].side);
  if (keeper && tactic) {
    rules.scoring.loftedBias =
        directnessBias(tactic->principles().goalkeeper.directness, distribution);
  }
  PassCandidateRules ground = rules;
  if (current.ball().held) {
    ground.scoring.maxPassDistance =
        std::min(ground.scoring.maxPassDistance, distribution.throwRange);
  }
  std::vector<PassCandidate> candidates =
      generatePassCandidates(current, carrier, context.tick(), context.secondsPerTick(), ground);
  if (keeper) {
    const std::vector<PassCandidate> longBalls = generateLongBallCandidates(
        current, carrier, context.tick(), context.secondsPerTick(), rules, distribution);
    candidates.insert(candidates.end(), longBalls.begin(), longBalls.end());
    orderPassCandidates(candidates);
  }
  return candidates;
}

}  // namespace

std::optional<OnBallChoice> chooseOnBall(const std::span<const PassCandidate> passes,
                                         const std::span<const ShotCandidate> shots,
                                         const double temperature,
                                         SimCore::RandomNumberGenerator& random) {
  // Valid candidates come first, the best of them at the front.
  std::vector<double> utilities;
  for (const PassCandidate& pass : passes) {
    if (!pass.isValid()) {
      break;
    }
    utilities.push_back(pass.utility);
  }
  const std::size_t passCount = utilities.size();
  if (!shots.empty() && shots.front().isValid()) {
    utilities.push_back(shots.front().utility);
  }
  const auto chosen = chooseByUtility(utilities, temperature, random);
  if (!chosen) {
    return std::nullopt;
  }
  if (*chosen < passCount) {
    return OnBallChoice{.outcome = DecisionOutcome::kPassed, .index = *chosen};
  }
  return OnBallChoice{.outcome = DecisionOutcome::kShot, .index = 0};
}

PassScoringConfig scoringForRisk(const PassScoringConfig& scoring,
                                 const double passingRisk) noexcept {
  PassScoringConfig adjusted = scoring;
  adjusted.progressionWeight = scoring.progressionWeight * (0.5 + passingRisk);
  adjusted.riskWeight = scoring.riskWeight * (1.5 - passingRisk);
  adjusted.minCompletion = std::min(1.0, scoring.minCompletion * (1.3 - (0.6 * passingRisk)));
  return adjusted;
}

ShotScoringConfig shotScoringForRisk(const ShotScoringConfig& scoring,
                                     const double passingRisk) noexcept {
  ShotScoringConfig adjusted = scoring;
  adjusted.goalWeight = scoring.goalWeight * (0.5 + passingRisk);
  adjusted.lossWeight = scoring.lossWeight * (1.5 - passingRisk);
  adjusted.minGoalChance = std::min(1.0, scoring.minGoalChance * (1.3 - (0.6 * passingRisk)));
  return adjusted;
}

MatchSystem makePassDecisionSystem(const DecisionConfig& config, const PassCandidateRules& rules) {
  validate(config);
  PassCandidateRules scored = rules;
  scored.scoring = config.scoring;
  const ShotCandidateRules shooting{.scoring = config.shooting,
                                    .ball = rules.ball,
                                    .perception = rules.perception,
                                    .reception = rules.reception};
  return {.name = std::string(kPassDecisionSystemName),
          .update =
              [config, scored, shooting](const MatchStepContext& context, const MatchState& current,
                                         MatchStateWriter& next) {
                const BallState& ball = current.ball();
                if (!ball.owner) {
                  return;
                }
                const auto carrier = findPlayerIndex(current, *ball.owner);
                if (!carrier || current.pendingAction(*carrier)) {
                  return;
                }
                if (heldSeconds(ball, context.tick(), context.secondsPerTick()) <
                    holdSeconds(ball, config)) {
                  return;
                }
                PassCandidateRules carrierRules = scored;
                ShotCandidateRules shooterRules = shooting;
                const TeamSide side = current.players()[*carrier].side;
                const auto& tactic = current.tactics().of(side);
                const auto& phase = current.phase(side);
                if (tactic && phase) {
                  const double risk = tactic->instruction(phase->phase).passingRisk;
                  carrierRules.scoring = scoringForRisk(scored.scoring, risk);
                  shooterRules.scoring = shotScoringForRisk(shooting.scoring, risk);
                }
                const std::vector<PassCandidate> candidates =
                    passOptions(context, current, *carrier, config.distribution, carrierRules);
                const std::vector<ShotCandidate> shots = generateShotCandidates(
                    current, *carrier, context.tick(), context.secondsPerTick(), shooterRules);
                const auto chosen =
                    chooseOnBall(candidates, shots, config.temperature,
                                 context.random(SimCore::RandomNumberGeneratorDomain::kAi));
                // Built only on request, and after the choice: diagnostics can
                // neither change the decision nor draw a number.
                if (context.collectsDiagnostics(*ball.owner)) {
                  context.diagnose(DecisionDiagnostic{
                      .tick = context.tick(),
                      .player = *ball.owner,
                      .observations = current.perception(*carrier).observations,
                      .candidates = candidates,
                      .shots = shots,
                      .outcome = chosen ? chosen->outcome : DecisionOutcome::kNoValidOption,
                      .chosen = chosen ? std::optional(chosen->index) : std::nullopt,
                      .scoring = carrierRules.scoring,
                      .shotScoring = shooterRules.scoring,
                      .held = ball.held});
                }
                if (!chosen) {
                  return;
                }
                if (chosen->outcome == DecisionOutcome::kShot) {
                  const ShotCandidate& shot = shots[chosen->index];
                  next.setPendingAction(*carrier,
                                        ShotIntent{.shooter = *ball.owner,
                                                   .target = shot.target,
                                                   .height = shot.height,
                                                   .speed = shooterRules.scoring.shotSpeed});
                  return;
                }
                const PassCandidate& pass = candidates[chosen->index];
                next.setPendingAction(*carrier, PassIntent{.passer = *ball.owner,
                                                           .target = pass.target,
                                                           .speed = pass.speed,
                                                           .receiver = pass.receiver,
                                                           .lofted = pass.lofted});
              },
          .intervalTicks = config.intervalTicks,
          .phaseTicks = 0};
}

}  // namespace ElyverseFootball::SimMatch
