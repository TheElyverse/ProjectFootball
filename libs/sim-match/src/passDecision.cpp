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
#include "carrying.hpp"
#include "choicePolicy.hpp"
#include "distribution.hpp"
#include "passCandidates.hpp"
#include "playerMovement.hpp"
#include "pursuit.hpp"
#include "shotCandidates.hpp"
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
  validate(config.carrying);
  validate(config.distribution);
}

// The steps a carrier's run after his touched ball predicts it in.
constexpr double kSteerSampleSeconds = 0.1;

// He runs on through where he meets his touched ball if it lies within 45
// degrees of where he runs.
constexpr double kRunThroughCosine = 0.7071067811865476;

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
    const SimCore::Vec2 target =
        clearanceTarget(current, carrier,
                        {.reach = reach,
                         .minDistance = config.distribution.minLongDistance,
                         .touchlineMargin = config.clearTouchlineMargin,
                         .goalLineMargin = config.clearGoalLineMargin});
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
                                                    const std::span<const CarryCandidate> carries,
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
  if (chosen->outcome == DecisionOutcome::kHeld) {
    return std::nullopt;
  }
  if (chosen->outcome != DecisionOutcome::kPassed) {
    const CarryCandidate& carry = carries[chosen->index];
    return CarryIntent{.carrier = carrier,
                       .target = carry.touchTarget,
                       .speed = carry.touchSpeed,
                       .seconds = carry.touchSeconds,
                       .errorFactor = carry.errorFactor};
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

// What choosing a carry of this kind is.
[[nodiscard]] DecisionOutcome carryOutcome(const CarryKind kind) noexcept {
  switch (kind) {
    case CarryKind::kHold:
      return DecisionOutcome::kHeld;
    case CarryKind::kTurn:
      return DecisionOutcome::kTurned;
    case CarryKind::kCarry:
      return DecisionOutcome::kCarried;
    case CarryKind::kDribble:
      return DecisionOutcome::kDribbled;
  }
  return DecisionOutcome::kHeld;
}

// The carry the choice turns, carries or dribbles with -- playing the ball on
// with a touch --; nullptr for any other choice.
[[nodiscard]] const CarryCandidate* touchOf(const std::optional<OnBallChoice>& chosen,
                                            const std::span<const CarryCandidate> carries) {
  return chosen && isTouch(chosen->outcome) ? &carries[chosen->index] : nullptr;
}

// Whether a carry gains something: ground toward the goal's centre of at
// least minCarryDistance, a better shot or an opponent bypassed.
[[nodiscard]] bool gains(const CarryCandidate& carry, const Pitch& pitch,
                         const CarryScoringConfig& scoring) noexcept {
  return carry.progression * pitch.lengthMeters() >= scoring.minCarryDistance || carry.shot > 0.0 ||
         carry.bypassed > 0;
}

// Where a carrier looks first while his ball rolls ahead of him: at his best
// pass's receiver or at the goal if his best shot is better, at the carry's
// target without either.
[[nodiscard]] SimCore::Vec2 focusOf(const MatchState& current, const std::size_t carrier,
                                    const std::span<const PassCandidate> passes,
                                    const std::span<const ShotCandidate> shots,
                                    const CarryCandidate& carry) {
  const bool pass = !passes.empty() && passes.front().isValid();
  const bool shot = !shots.empty() && shots.front().isValid();
  if (shot && (!pass || shots.front().utility > passes.front().utility)) {
    return attackedGoal(current.pitch(), current.players()[carrier].side).center;
  }
  return pass ? passes.front().target : carry.target;
}

// The run a turn, carry or dribble starts or continues: one of the same kind
// carries on from when it began, a dribble always begins anew.
[[nodiscard]] CarryRun runOf(const MatchStepContext& context, const MatchState& current,
                             const std::size_t carrier, const CarryCandidate& carry,
                             const SimCore::Vec2 focus) {
  const CarryRun* active = activeCarry(current, carrier);
  const PlayerMatchState& player = current.players()[carrier];
  const bool continues =
      active != nullptr && active->kind == carry.kind && carry.kind != CarryKind::kDribble;
  return CarryRun{.kind = carry.kind,
                  .target = carry.target,
                  .subject = carry.subject,
                  .since = continues ? active->since : context.tick(),
                  .focus = focus,
                  .gaze = SimCore::unitOr(focus - player.position, player.facing),
                  .touchedAt = active != nullptr ? active->touchedAt : std::nullopt,
                  .retakenAt = active != nullptr ? active->retakenAt : std::nullopt,
                  .dribbling = carry.kind == CarryKind::kDribble};
}

// The carrier after his touched ball, every tick it rolls ahead of him: he
// runs to where he meets it -- on through it along his run -- and looks about
// for his next decision -- at his
// run's focus until the touch's seconds have passed, then at his first look
// target, and back at the ball once he is about to meet it.
void followTouch(const MatchStepContext& context, const MatchState& current,
                 const std::size_t carrier, const CarryScoringConfig& scoring,
                 const PassCandidateRules& rules, MatchStateWriter& next) {
  const BallState& ball = current.ball();
  const CarryRun* run = activeCarry(current, carrier);
  if (run == nullptr || !ball.touch) {
    return;
  }
  const PlayerMatchState runner = atRunningSpeed(current.players()[carrier], ball, rules.ball);
  const auto meet = findInterception(runner, ball, rules.ball, current.pitch(),
                                     {.intervalTicks = 1,
                                      .sampleSeconds = kSteerSampleSeconds,
                                      .horizonSeconds = scoring.horizonSeconds},
                                     rules.reception.controlHeight);
  // Where he meets the ball lies along his run, he runs on through it, as far
  // again as his run still goes beyond it -- not at all past its target --,
  // rather than braking for it; elsewhere he brakes to take it.
  if (meet) {
    const SimCore::Vec2 toward = SimCore::unitOr(meet->point - runner.position, {});
    const bool ahead = toward.dot(SimCore::unitOr(runner.velocity, toward)) >= kRunThroughCosine;
    const double onward = ahead ? std::max(0.0, (run->target - meet->point).dot(toward)) : 0.0;
    next.setPlayerTarget(carrier, current.pitch().clamp(meet->point + (toward * onward)));
  }
  SimCore::Vec2 lookAt = run->focus;
  if (!meet || meet->seconds <= scoring.lookBackSeconds) {
    lookAt = ball.position;
  } else if (ball.touch->hasPassed(context.tick(), context.secondsPerTick())) {
    if (const std::vector<LookTarget> targets = lookTargets(current, carrier); !targets.empty()) {
      lookAt = targets.front().point;
    }
  }
  if (auto& carry = next.tactical(carrier).carry) {
    carry->gaze = SimCore::unitOr(lookAt - runner.position, run->gaze);
  }
}

// What the decision system decides with.
struct OnBallRules {
  DecisionConfig config;
  PassCandidateRules passes;
  ShotCandidateRules shots;
  CarryCandidateRules carries;
};

// Whether the carrier at this index decides in this step: with the ball at
// his feet or in his hands and no action pending, at the interval and in the
// tick after he got his touched ball back -- but not while he keeps a ball he
// has just taken, which his own touches do not make him.
[[nodiscard]] bool decidesNow(const MatchStepContext& context, const MatchState& current,
                              const std::size_t carrier, const DecisionConfig& config) {
  const BallState& ball = current.ball();
  if (ball.touch || current.pendingAction(carrier)) {
    return false;
  }
  const CarryRun* run = activeCarry(current, carrier);
  const bool retaken =
      run != nullptr && run->retakenAt && run->retakenAt->value() + 1 == context.tick().value();
  if (!retaken && context.tick().value() % config.intervalTicks != 0) {
    return false;
  }
  return run != nullptr ||
         heldSeconds(ball, context.tick(), context.secondsPerTick()) >= holdSeconds(ball, config);
}

// A carrier's options, and the rules they were scored with: for the passing
// risk of his side's tactic in its current phase, his carries for the phase
// itself too, if it has one.
struct OnBallOptions {
  OnBallRules rules;
  std::vector<PassCandidate> passes;
  std::vector<ShotCandidate> shots;
  std::vector<CarryCandidate> carries;
};

[[nodiscard]] OnBallOptions optionsOf(const MatchStepContext& context, const MatchState& current,
                                      const std::size_t carrier, const OnBallRules& rules) {
  OnBallOptions options{.rules = rules, .passes = {}, .shots = {}, .carries = {}};
  const TeamSide side = current.players()[carrier].side;
  const auto& tactic = current.tactics().of(side);
  const auto& phase = current.phase(side);
  if (tactic && phase) {
    const double risk = tactic->instruction(phase->phase).passingRisk;
    options.rules.passes.scoring = scoringForRisk(rules.passes.scoring, risk);
    options.rules.shots.scoring = shotScoringForRisk(rules.shots.scoring, risk);
    options.rules.carries.scoring =
        carryScoringForRisk(carryScoringForPhase(rules.carries.scoring, phase->phase), risk);
  }
  options.passes =
      passOptions(context, current, carrier, rules.config.distribution, options.rules.passes);
  options.shots = generateShotCandidates(current, carrier, context.tick(), context.secondsPerTick(),
                                         options.rules.shots);
  options.carries = generateCarryCandidates(current, carrier, context.tick(),
                                            context.secondsPerTick(), options.rules.carries);
  return options;
}

// Holding the ball, or carrying it without gaining anything, he looks for an
// option -- a better one if he has any -- and once stuck plays the ball
// either way, which replaces his choice. Carrying it on with a gain, his scan
// keeps its time: it ends only when he plays or loses the ball.
[[nodiscard]] ScanStep lookAround(const MatchStepContext& context, const MatchState& current,
                                  const std::size_t carrier, const OnBallOptions& options,
                                  std::optional<OnBallChoice>& chosen) {
  const DecisionConfig& config = options.rules.config;
  ScanStep step;
  if (current.ball().held) {
    return step;
  }
  const CarryCandidate* touch = touchOf(chosen, options.carries);
  const bool playing = chosen && (chosen->outcome == DecisionOutcome::kPassed ||
                                  chosen->outcome == DecisionOutcome::kShot);
  if (touch != nullptr && gains(*touch, current.pitch(), config.carrying)) {
    if (const CarrierScan* active = activeScan(current, carrier)) {
      step.scan = *active;
    }
    return step;
  }
  if (playing) {
    return step;
  }
  step = scanStep(context, current, carrier, options.passes, config, options.rules.passes);
  if (step.chosen || step.reason == OnBallReason::kForcedClear) {
    chosen = step.chosen;
  } else if (touch != nullptr) {
    step.reason = OnBallReason::kChoice;
    step.target.reset();
  }
  return step;
}

// One decision of the player on the ball at this index (makePassDecisionSystem()).
void decideOnBall(const MatchStepContext& context, const MatchState& current,
                  const std::size_t carrier, const OnBallRules& rules, MatchStateWriter& next) {
  const BallState& ball = current.ball();
  const SimCore::PlayerId player = current.players()[carrier].playerId;
  const OnBallOptions options = optionsOf(context, current, carrier, rules);
  auto chosen =
      chooseOnBall(options.passes, options.shots, options.carries, rules.config.temperature,
                   context.random(SimCore::RandomNumberGeneratorDomain::kAi));
  if (!chosen && ball.held &&
      heldSeconds(ball, context.tick(), context.secondsPerTick()) >=
          rules.config.distribution.clearSeconds) {
    chosen = clearance(options.passes);
  }
  const ScanStep step = lookAround(context, current, carrier, options, chosen);
  next.tactical(carrier).scan = step.scan;
  const std::optional<PendingAction> action =
      actionOf(player, chosen, step, options.passes, options.shots, options.carries,
               options.rules.shots.scoring.shotSpeed);
  std::optional<SimCore::Vec2> target = step.target;
  if (const CarryCandidate* touch = touchOf(chosen, options.carries)) {
    const CarryCandidate& carry = *touch;
    next.tactical(carrier).carry =
        runOf(context, current, carrier, carry,
              focusOf(current, carrier, options.passes, options.shots, carry));
    next.setPlayerTarget(carrier, carry.touchTarget);
    target = carry.target;
  } else if (chosen && chosen->outcome == DecisionOutcome::kHeld) {
    next.setPlayerTarget(carrier, std::nullopt);
  }
  // Built only on request, and after the choice: diagnostics can neither
  // change the decision nor draw a number.
  if (context.collectsDiagnostics(player)) {
    context.diagnose(
        DecisionDiagnostic{.tick = context.tick(),
                           .player = player,
                           .observations = current.perception(carrier).observations,
                           .candidates = options.passes,
                           .shots = options.shots,
                           .outcome = outcomeOf(chosen, action.has_value()),
                           .chosen = chosen ? std::optional(chosen->index) : std::nullopt,
                           .scoring = options.rules.passes.scoring,
                           .shotScoring = options.rules.shots.scoring,
                           .carries = options.carries,
                           .carryScoring = options.rules.carries.scoring,
                           .held = ball.held,
                           .reason = step.reason,
                           .target = target});
  }
  if (action) {
    next.setPendingAction(carrier, action);
  }
}

}  // namespace

std::optional<OnBallChoice> chooseOnBall(const std::span<const PassCandidate> passes,
                                         const std::span<const ShotCandidate> shots,
                                         const double temperature,
                                         SimCore::RandomNumberGenerator& random) {
  return chooseOnBall(passes, shots, {}, temperature, random);
}

std::optional<OnBallChoice> chooseOnBall(const std::span<const PassCandidate> passes,
                                         const std::span<const ShotCandidate> shots,
                                         const std::span<const CarryCandidate> carries,
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
  // The best valid carry of each kind competes, not every direction: the
  // list is ordered by utility, so it is the first of its kind.
  const std::size_t carryStart = utilities.size();
  std::vector<std::size_t> carryIndices;
  for (std::size_t index = 0; index < carries.size() && carries[index].isValid(); ++index) {
    const bool first = std::ranges::none_of(carryIndices, [&](const std::size_t kept) {
      return carries[kept].kind == carries[index].kind;
    });
    if (first) {
      carryIndices.push_back(index);
      utilities.push_back(carries[index].utility);
    }
  }
  const auto chosen = chooseByUtility(utilities, temperature, random);
  if (!chosen) {
    return std::nullopt;
  }
  if (*chosen < passCount) {
    return OnBallChoice{.outcome = DecisionOutcome::kPassed, .index = *chosen};
  }
  if (*chosen < carryStart) {
    return OnBallChoice{.outcome = DecisionOutcome::kShot, .index = 0};
  }
  const std::size_t index = carryIndices[*chosen - carryStart];
  return OnBallChoice{.outcome = carryOutcome(carries[index].kind), .index = index};
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
  OnBallRules onBall{.config = config,
                     .passes = rules,
                     .shots = {.scoring = config.shooting,
                               .ball = rules.ball,
                               .perception = rules.perception,
                               .reception = rules.reception},
                     .carries = {.scoring = config.carrying,
                                 .ball = rules.ball,
                                 .passing = rules.passing,
                                 .perception = rules.perception,
                                 .reception = rules.reception}};
  onBall.passes.scoring = config.scoring;
  return {
      .name = std::string(kPassDecisionSystemName),
      .update =
          [onBall](const MatchStepContext& context, const MatchState& current,
                   MatchStateWriter& next) {
            const BallState& ball = current.ball();
            const auto carrier = ball.owner ? findPlayerIndex(current, *ball.owner) : std::nullopt;
            if (!carrier) {
              return;
            }
            if (ball.touch) {
              followTouch(context, current, *carrier, onBall.config.carrying, onBall.passes, next);
            } else if (decidesNow(context, current, *carrier, onBall.config)) {
              decideOnBall(context, current, *carrier, onBall, next);
            }
          },
      .intervalTicks = 1,
      .phaseTicks = 0};
}

}  // namespace ElyverseFootball::SimMatch
