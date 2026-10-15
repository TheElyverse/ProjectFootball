#include "passDecision.hpp"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

#include "ballPhysics.hpp"
#include "carrierScan.hpp"
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
                     config.temperature <= kMax && config.stuckSeconds >= 0.0 &&
                     config.stuckSeconds <= kMax && config.clearTouchlineMargin >= 0.0 &&
                     config.clearTouchlineMargin <= kMax && config.clearGoalLineMargin >= 0.0 &&
                     config.clearGoalLineMargin <= kMax;
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

// Of the passes `accept` lets through, the one with the highest utility,
// ties to the first. Empty without such a pass.
template <typename Accept>
[[nodiscard]] std::optional<OnBallChoice> bestPass(const std::span<const PassCandidate> passes,
                                                   const Accept accept) {
  std::optional<OnBallChoice> best;
  for (std::size_t index = 0; index < passes.size(); ++index) {
    if (accept(passes[index]) && (!best || passes[index].utility > passes[best->index].utility)) {
      best = OnBallChoice{.outcome = DecisionOutcome::kPassed, .index = index};
    }
  }
  return best;
}

// A keeper's clearance when nothing is on: of the passes rejected only as
// too unlikely to arrive, the one with the highest utility.
[[nodiscard]] std::optional<OnBallChoice> clearance(const std::span<const PassCandidate> passes) {
  return bestPass(
      passes, [](const PassCandidate& pass) { return pass.rejection == PassRejection::kUnlikely; });
}

// What a carrier without a valid option does in one decision: he looks at
// his next look target, keeping his scan, or -- stuck for long enough -- plays
// his best rejected pass or clears the ball, ending it.
struct ScanStep {
  OnBallReason reason = OnBallReason::kChoice;
  // The point he looks at or clears the ball to.
  std::optional<SimCore::Vec2> target = std::nullopt;
  std::optional<OnBallChoice> chosen = std::nullopt;
  // The speed along the ground of his clearance, to target; 0 if he does
  // not clear the ball.
  double clearSpeed = 0.0;
  // His scan from now on; empty once he acts.
  std::optional<CarrierScan> scan = std::nullopt;
};

[[nodiscard]] ScanStep scanStep(const MatchStepContext& context, const MatchState& current,
                                const std::size_t carrier,
                                const std::span<const PassCandidate> passes,
                                const DecisionConfig& config, const PassCandidateRules& rules) {
  const PlayerMatchState& player = current.players()[carrier];
  const CarrierScan* active = activeScan(current, carrier);
  const SimCore::SimTick since = active != nullptr ? active->since : context.tick();
  const double stuck =
      static_cast<double>(context.tick().value() - since.value()) * context.secondsPerTick();
  if (stuck >= config.stuckSeconds) {
    if (const auto best = bestPass(passes, [](const PassCandidate&) { return true; })) {
      return {.reason = OnBallReason::kForcedRejected, .chosen = best};
    }
    // His longest kick: the speed along the ground a flight needs grows in
    // proportion to its distance.
    const double flightSeconds = config.distribution.flightSeconds;
    const double reach =
        rules.passing.maxLoftedSpeed / flightSpeed(1.0, flightSeconds, rules.ball).value_or(1.0);
    const SimCore::Vec2 target = clearanceTarget(
        current, carrier, reach, config.clearTouchlineMargin, config.clearGoalLineMargin);
    const auto speed =
        flightSpeed(SimCore::distance(current.ball().position, target), flightSeconds, rules.ball);
    if (speed) {
      return {.reason = OnBallReason::kForcedClear, .target = target, .clearSpeed = *speed};
    }
  }
  const int looks = active != nullptr ? active->looks : 0;
  ScanStep step{.scan = CarrierScan{.since = since,
                                    .looks = looks + 1,
                                    .gaze = active != nullptr ? active->gaze : player.facing}};
  const std::vector<LookTarget> targets = lookTargets(current, carrier);
  if (!targets.empty()) {
    const LookTarget& look = targets[static_cast<std::size_t>(looks) % targets.size()];
    step.reason = look.firstSide ? OnBallReason::kScanFirstSide : OnBallReason::kScanOtherSide;
    step.target = look.point;
    step.scan->gaze = SimCore::unitOr(look.point - player.position, step.scan->gaze);
  }
  return step;
}

// What the carrier's decision comes to: his clearance, or the chosen pass or
// shot; empty while he keeps the ball.
[[nodiscard]] std::optional<PendingAction> actionOf(const SimCore::PlayerId carrier,
                                                    const std::optional<OnBallChoice>& chosen,
                                                    const ScanStep& step,
                                                    const std::span<const PassCandidate> passes,
                                                    const std::span<const ShotCandidate> shots,
                                                    const double shotSpeed) {
  if (step.reason == OnBallReason::kForcedClear) {
    return PassIntent{.passer = carrier,
                      .target = step.target.value_or(SimCore::Vec2{}),
                      .speed = step.clearSpeed,
                      .receiver = std::nullopt,
                      .lofted = true};
  }
  if (!chosen) {
    return std::nullopt;
  }
  if (chosen->outcome == DecisionOutcome::kShot) {
    const ShotCandidate& shot = shots[chosen->index];
    return ShotIntent{
        .shooter = carrier, .target = shot.target, .height = shot.height, .speed = shotSpeed};
  }
  const PassCandidate& pass = passes[chosen->index];
  return PassIntent{.passer = carrier,
                    .target = pass.target,
                    .speed = pass.speed,
                    .receiver = pass.receiver,
                    .lofted = pass.lofted};
}

// The diagnostics' outcome: the choice's, a pass for a clearance, or none.
[[nodiscard]] DecisionOutcome outcomeOf(const std::optional<OnBallChoice>& chosen,
                                        const bool acting) noexcept {
  if (chosen) {
    return chosen->outcome;
  }
  return acting ? DecisionOutcome::kPassed : DecisionOutcome::kNoValidOption;
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
                auto chosen =
                    chooseOnBall(candidates, shots, config.temperature,
                                 context.random(SimCore::RandomNumberGeneratorDomain::kAi));
                if (!chosen && ball.held &&
                    heldSeconds(ball, context.tick(), context.secondsPerTick()) >=
                        config.distribution.clearSeconds) {
                  chosen = clearance(candidates);
                }
                ScanStep step;
                if (!chosen && !ball.held) {
                  step = scanStep(context, current, *carrier, candidates, config, carrierRules);
                  chosen = step.chosen;
                }
                next.tactical(*carrier).scan = step.scan;
                const std::optional<PendingAction> action = actionOf(
                    *ball.owner, chosen, step, candidates, shots, shooterRules.scoring.shotSpeed);
                // Built only on request, and after the choice: diagnostics can
                // neither change the decision nor draw a number.
                if (context.collectsDiagnostics(*ball.owner)) {
                  context.diagnose(DecisionDiagnostic{
                      .tick = context.tick(),
                      .player = *ball.owner,
                      .observations = current.perception(*carrier).observations,
                      .candidates = candidates,
                      .shots = shots,
                      .outcome = outcomeOf(chosen, action.has_value()),
                      .chosen = chosen ? std::optional(chosen->index) : std::nullopt,
                      .scoring = carrierRules.scoring,
                      .shotScoring = shooterRules.scoring,
                      .held = ball.held,
                      .reason = step.reason,
                      .target = step.target});
                }
                if (action) {
                  next.setPendingAction(*carrier, action);
                }
              },
          .intervalTicks = config.intervalTicks,
          .phaseTicks = 0};
}

}  // namespace ElyverseFootball::SimMatch
