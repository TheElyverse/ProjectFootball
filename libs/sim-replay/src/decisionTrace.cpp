#include "decisionTrace.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <format>
#include <limits>
#include <string>
#include <utility>
#include <variant>

#include "actionCandidate.hpp"
#include "crossCandidates.hpp"
#include "observation.hpp"
#include "passCandidates.hpp"
#include "perception.hpp"
#include "shotCandidates.hpp"
#include "tacticalState.hpp"

namespace ElyverseFootball::SimReplay {
namespace {

using SimMatch::DecisionDiagnostic;
using SimMatch::MatchEvent;

// An outcome without a failure to explain.
[[nodiscard]] PassOutcome settled(const PassResult result,
                                  const std::optional<SimCore::SimTick> tick,
                                  const std::optional<SimCore::PlayerId> player) {
  return {.result = result,
          .tick = tick,
          .by = player,
          .cause = std::nullopt,
          .believedMetersOff = std::nullopt};
}

// Signed, to two decimals; what rounds to zero reads "+0.00", never "-0.00".
[[nodiscard]] std::string signedNumber(const double value) {
  return std::format("{:+.2f}", std::abs(value) < 0.005 ? 0.0 : value);
}

// "#7" for a player, "-" for nobody.
[[nodiscard]] std::string playerText(const std::optional<SimCore::PlayerId>& player) {
  return player ? std::format("#{}", player->value()) : std::string("-");
}

[[nodiscard]] std::string outcomeText(const PassOutcome& outcome) {
  std::string text = std::format(" -> {}", passResultName(outcome.result));
  if (outcome.by) {
    text += std::format(" by {}", playerText(outcome.by));
  }
  if (outcome.tick) {
    text += std::format(" at t={}", outcome.tick->value());
  }
  if (outcome.cause) {
    text += std::format(": {}", failureCauseName(*outcome.cause));
    if (outcome.cause == FailureCause::kPerception) {
      text += outcome.believedMetersOff
                  ? std::format(" (believed {:.1f} m off)", *outcome.believedMetersOff)
                  : std::string(" (not seen)");
    }
  }
  return text;
}

// ", judged 0.31" when a poor decider misjudged an estimate
// (docs/on-ball.md#decision-making), empty otherwise.
[[nodiscard]] std::string judgedNote(const double estimate, const double judged) {
  return judged != estimate ? std::format(", judged {:.2f}", judged) : std::string();
}

// Why he did not shoot: that the best shot lost the choice, or why even the
// best zone of the goal was not on. Empty if he had no shot to consider.
[[nodiscard]] std::string shotNote(const DecisionDiagnostic& decision) {
  if (decision.shots.empty()) {
    return {};
  }
  const SimMatch::ShotCandidate& best = decision.shots.front();
  if (best.isValid()) {
    return std::format("; best shot (utility {:.2f}) not chosen", best.utility);
  }
  return SimMatch::isScored(best.rejection)
             ? std::format("; no shot: {} (goal chance {:.2f}{})",
                           SimMatch::shotRejectionName(best.rejection), best.goalChance,
                           judgedNote(best.goalChance, best.judgedGoalChance))
             : std::format("; no shot: {}", SimMatch::shotRejectionName(best.rejection));
}

[[nodiscard]] std::string shotLine(const DecisionDiagnostic& decision, const std::size_t chosen,
                                   const std::string& line, const std::string& context) {
  const SimMatch::ShotCandidate& shot = decision.shots.at(chosen);
  const SimMatch::ShotContributions parts = SimMatch::shotContributions(shot, decision.shotScoring);
  // A driven shot reads "shoots at", a curled one "curls a shot at", a chip
  // "chips at" (docs/on-ball.md#shot-types).
  std::string_view verb = "shoots at";
  if (shot.type == SimMatch::ShotType::kCurled) {
    verb = "curls a shot at";
  } else if (shot.type == SimMatch::ShotType::kChipped) {
    verb = "chips at";
  }
  return line + std::format(
                    "{} ({:.1f}, {:.1f}) {:.2f} m high (utility {:.2f}: goal {} "
                    "secondBall {} possession {}; goal chance {:.2f}{}, save {:.2f}{}, block "
                    "{:.2f}{}; {}) because {}",
                    verb, shot.target.x, shot.target.y, shot.height, shot.utility,
                    signedNumber(parts.goal), signedNumber(parts.secondBall),
                    signedNumber(parts.possession), shot.goalChance,
                    judgedNote(shot.goalChance, shot.judgedGoalChance), shot.saveRisk,
                    judgedNote(shot.saveRisk, shot.judgedSaveRisk), shot.blockRisk,
                    judgedNote(shot.blockRisk, shot.judgedBlockRisk), context,
                    SimMatch::dominantShotContribution(parts));
}

// A cross (docs/on-ball.md#crossing): the area, where and how high it is
// meant to arrive, for whom, and the parts of its utility.
[[nodiscard]] std::string crossLine(const PassDecisionTrace& trace, const std::string& line,
                                    const std::string& context) {
  const DecisionDiagnostic& decision = trace.decision;
  const SimMatch::CrossCandidate& cross = decision.crosses.at(decision.chosen.value_or(0));
  const SimMatch::CrossContributions parts =
      SimMatch::crossContributions(cross, decision.crossScoring);
  return line +
         std::format(
             "crosses to the {} ({:.1f}, {:.1f}) {:.2f} m high for {} (utility {:.2f}: "
             "win {} threat {} loss {}; win chance {:.2f}, keeper {:.2f}; {}) because {}",
             SimMatch::crossAreaName(cross.area), cross.target.x, cross.target.y, cross.height,
             playerText(cross.runner), cross.utility, signedNumber(parts.win),
             signedNumber(parts.threat), signedNumber(parts.loss), cross.winChance,
             cross.keeperClaim, context, SimMatch::dominantCrossContribution(parts)) +
         outcomeText(trace.outcome);
}

// What he does with the ball at his feet instead of passing or shooting
// (docs/on-ball.md#ball-carrying), with the parts of its utility.
[[nodiscard]] std::string carryLine(const DecisionDiagnostic& decision, const std::size_t chosen,
                                    const std::string& line, const std::string& context) {
  const SimMatch::CarryCandidate& carry = decision.carries.at(chosen);
  const SimMatch::CarryContributions parts =
      SimMatch::carryContributions(carry, decision.carryScoring);
  if (carry.kind == SimMatch::CarryKind::kHold) {
    return line + std::format("holds the ball (utility {:.2f}: hold {}; pressure {:.2f}; {}{})",
                              carry.utility, signedNumber(parts.hold), carry.risk, context,
                              shotNote(decision));
  }
  std::string verb = carry.kind == SimMatch::CarryKind::kTurn ? "turns toward" : "carries to";
  if (carry.kind == SimMatch::CarryKind::kDribble) {
    verb = std::format("dribbles past {} to", playerText(carry.subject));
  }
  return line + std::format(
                    "{} ({:.1f}, {:.1f}) (utility {:.2f}: keep {} progression {} space {} shot {} "
                    "bypassed {} risk {}{}; estimated risk {:.2f}; {}{}) because {}",
                    verb, carry.target.x, carry.target.y, carry.utility, signedNumber(parts.keep),
                    signedNumber(parts.progression), signedNumber(parts.space),
                    signedNumber(parts.shot), signedNumber(parts.bypassed),
                    signedNumber(parts.risk),
                    carry.kind == SimMatch::CarryKind::kTurn
                        ? std::format(" turn {}", signedNumber(parts.turn))
                        : std::string(),
                    carry.risk, context, shotNote(decision), SimMatch::dominantContribution(parts));
}

// Whether the decision held, turned, carried or dribbled.
[[nodiscard]] bool carried(const DecisionDiagnostic& decision) noexcept {
  return decision.outcome == SimMatch::DecisionOutcome::kHeld ||
         SimMatch::isTouch(decision.outcome);
}

// Whether he had a valid pass or shot: holding the ball despite one, he
// looks for a better option.
[[nodiscard]] bool hadValidOption(const DecisionDiagnostic& decision) noexcept {
  return std::ranges::any_of(decision.candidates,
                             [](const SimMatch::PassCandidate& pass) { return pass.isValid(); }) ||
         std::ranges::any_of(decision.shots,
                             [](const SimMatch::ShotCandidate& shot) { return shot.isValid(); }) ||
         std::ranges::any_of(decision.crosses,
                             [](const SimMatch::CrossCandidate& cross) { return cross.isValid(); });
}

// How the ball goes: a pass along the ground or a long kick, from the hands
// a throw or a punt (docs/goalkeeper.md#distribution).
[[nodiscard]] std::string_view passVerb(const SimMatch::PassCandidate& pass, const bool held) {
  if (pass.lofted) {
    return held ? "punts to" : "kicks long to";
  }
  return held ? "throws to" : "passes to";
}

[[nodiscard]] std::string passLine(const PassDecisionTrace& trace) {
  const DecisionDiagnostic& decision = trace.decision;
  std::string line = std::format("t={} #{} ", decision.tick.value(), decision.player.value());
  const std::string context = std::format("{} options, {} observed", decision.candidates.size(),
                                          decision.observations.size());
  if (decision.outcome == SimMatch::DecisionOutcome::kShot && decision.chosen &&
      *decision.chosen < decision.shots.size()) {
    return shotLine(decision, *decision.chosen, line, context);
  }
  if (decision.outcome == SimMatch::DecisionOutcome::kCrossed && decision.chosen &&
      *decision.chosen < decision.crosses.size()) {
    return crossLine(trace, line, context);
  }
  const SimMatch::OnBallReason reason = decision.reason;
  // Holding the ball while he looks for an option reads as keeping it.
  const bool looking = decision.outcome == SimMatch::DecisionOutcome::kHeld &&
                       reason != SimMatch::OnBallReason::kChoice;
  if (carried(decision) && !looking && decision.chosen &&
      *decision.chosen < decision.carries.size()) {
    return carryLine(decision, *decision.chosen, line, context);
  }
  if (reason == SimMatch::OnBallReason::kForcedClear && decision.target) {
    // Stuck without a pass at all, or pressed near his goal with one
    // (docs/on-ball.md#clearances).
    const bool anyPass = std::ranges::any_of(
        decision.candidates, [](const SimMatch::PassCandidate& pass) { return pass.isValid(); });
    return line +
           std::format("clears the ball to ({:.1f}, {:.1f}): {} ({}; {})", decision.target->x,
                       decision.target->y, anyPass ? "pressed near his goal" : "no pass to play",
                       context, SimMatch::onBallReasonName(reason)) +
           outcomeText(trace.outcome);
  }
  if (!decision.chosen || looking || *decision.chosen >= decision.candidates.size()) {
    const std::string look = decision.target ? std::format("; {} toward ({:.1f}, {:.1f})",
                                                           SimMatch::onBallReasonName(reason),
                                                           decision.target->x, decision.target->y)
                                             : std::string();
    return line +
           std::format("keeps the ball: {} ({}{}{})",
                       hadValidOption(decision) ? "looks for a better option" : "no valid option",
                       context, shotNote(decision), look);
  }
  const SimMatch::PassCandidate& pass = decision.candidates.at(*decision.chosen);
  const SimMatch::PassContributions parts = SimMatch::passContributions(pass, decision.scoring);
  const std::string directness =
      pass.lofted ? std::format(" directness {}", signedNumber(parts.directness)) : std::string();
  line += std::format(
      "{} #{} (utility {:.2f}: completion {} progression {} pressure {} risk {}{}; "
      "estimated risk {:.2f}{}; {}{}) because {}",
      passVerb(pass, decision.held), pass.receiver.value(), pass.utility,
      signedNumber(parts.completion), signedNumber(parts.progression), signedNumber(parts.pressure),
      signedNumber(parts.risk), directness, pass.interceptionRisk,
      judgedNote(pass.interceptionRisk, pass.judgedInterceptionRisk), context, shotNote(decision),
      reason == SimMatch::OnBallReason::kForcedRejected
          ? std::string("its best rejected option (forcedRejected)")
          : std::string(SimMatch::dominantContribution(parts)));
  return line + outcomeText(trace.outcome);
}

[[nodiscard]] std::string actionLine(const ActionDecisionTrace& trace) {
  const SimMatch::ActionDiagnostic& decision = trace.decision;
  std::string line = std::format("t={} #{} ", decision.tick.value(), decision.player.value());
  if (!decision.chosen || *decision.chosen >= decision.candidates.size()) {
    return line + "no action";
  }
  const SimMatch::ActionCandidate& action = decision.candidates.at(*decision.chosen);
  const SimMatch::ActionScores& scores = action.scores;
  line += std::format("{}", SimMatch::actionName(action.type));
  if (action.subject) {
    line += std::format(" {}", playerText(action.subject));
  }
  if (decision.assigned) {
    return line + " (assigned by the team press)";
  }
  if (decision.idlePress) {
    return line + " (idlePress: the carrier stands on the ball)";
  }
  return line + std::format(
                    " (utility {:.2f}: responsibility {} region {} space {} lane {} urgency {} "
                    "effort {}; {} options) because {}",
                    action.utility, signedNumber(scores.responsibility),
                    signedNumber(scores.region), signedNumber(scores.space),
                    signedNumber(scores.lane), signedNumber(scores.urgency),
                    signedNumber(scores.effort), decision.candidates.size(),
                    SimMatch::dominantScore(scores));
}

[[nodiscard]] std::string sweepLine(const SweepDecisionTrace& trace) {
  const SimMatch::SweepDiagnostic& decision = trace.decision;
  const SimMatch::SweepCall& call = decision.call;
  std::string line = std::format("t={} #{} {} (reaches the ball in {:.2f} s, ",
                                 decision.tick.value(), decision.player.value(),
                                 call.coming ? "sweeps" : "stays home", call.keeperSeconds);
  if (!call.attackerSeconds) {
    return line + "no attacker can)";
  }
  return line + std::format("the first attacker in {:.2f} s; misjudged by {} s, needs {} s)",
                            *call.attackerSeconds, signedNumber(call.misjudgement),
                            signedNumber(call.threshold));
}

// A duel move (docs/duels.md#duels-on-the-ball): the move he drew, its
// utility, every move he weighed with its own, and whether he drew the best.
[[nodiscard]] std::string duelLine(const DuelDecisionTrace& trace) {
  const SimMatch::DuelDiagnostic& decision = trace.decision;
  std::string line = std::format("t={} #{} ", decision.tick.value(), decision.player.value());
  if (!decision.chosen || *decision.chosen >= decision.options.size()) {
    return line + "no duel move";
  }
  const SimMatch::DuelOption& chosen = decision.options.at(*decision.chosen);
  std::string weighed;
  double best = chosen.utility;
  for (const SimMatch::DuelOption& option : decision.options) {
    weighed += std::format("{}{} {:.2f}", weighed.empty() ? "" : " ",
                           SimMatch::duelMoveName(option.move), option.utility);
    best = std::max(best, option.utility);
  }
  return line + std::format("{} in the duel {} #{} (utility {:.2f}: {}; {} options) because {}",
                            SimMatch::duelMoveName(chosen.move),
                            decision.carrier ? "against" : "with", decision.opponent.value(),
                            chosen.utility, weighed, decision.options.size(),
                            chosen.utility >= best ? "best" : "drawn");
}

// A keeper's dive (docs/goalkeeper.md#reacting-and-deciding): where he read
// the shot, the run he chose and how close it got him, against his margin.
[[nodiscard]] std::string diveLine(const DiveDecisionTrace& trace) {
  const SimMatch::DiveDiagnostic& decision = trace.decision;
  std::string line = std::format("t={} #{} ", decision.tick.value(), decision.player.value());
  const std::string read = std::format("read {} m across, {:.2f} m up",
                                       signedNumber(decision.readAcross), decision.readUp);
  double closest = std::numeric_limits<double>::infinity();
  for (const SimMatch::DiveRun& run : decision.runs) {
    closest = std::min(closest, run.gap);
  }
  if (decision.call == SimMatch::DiveCall::kLeaveWide) {
    return line + std::format("leaves #{}'s shot ({}) because {}", decision.shooter.value(), read,
                              SimMatch::diveCallName(decision.call));
  }
  if (!decision.chosen || *decision.chosen >= decision.runs.size()) {
    return line + std::format(
                      "leaves #{}'s shot (closest {:.2f} m, beyond {:.2f} m; {}; {} runs) "
                      "because {}",
                      decision.shooter.value(), closest, decision.tryMargin, read,
                      decision.runs.size(), SimMatch::diveCallName(decision.call));
  }
  const SimMatch::DiveRun& run = decision.runs.at(*decision.chosen);
  return line + std::format(
                    "dives at #{}'s shot after a run of {} m (gap {:.2f} m, within {:.2f} m; "
                    "{}; {} runs) because {}",
                    decision.shooter.value(), signedNumber(run.feet), run.gap, decision.tryMargin,
                    read, decision.runs.size(), SimMatch::diveCallName(decision.call));
}

}  // namespace

std::string_view failureCauseName(const FailureCause cause) noexcept {
  switch (cause) {
    case FailureCause::kPerception:
      return "perception";
    case FailureCause::kJudgement:
      return "judgement";
    case FailureCause::kDecision:
      return "decision";
    case FailureCause::kExecution:
      return "execution";
  }
  return "unknown";
}

std::string_view passResultName(const PassResult result) noexcept {
  switch (result) {
    case PassResult::kPending:
      return "pending";
    case PassResult::kNotPlayed:
      return "not played";
    case PassResult::kReceived:
      return "received";
    case PassResult::kIntercepted:
      return "intercepted";
    case PassResult::kRecoveredByPasser:
      return "recovered by the passer";
  }
  return "unknown";
}

DecisionTracer::DecisionTracer(const SimMatch::MatchConfig& config, const TraceConfig traceConfig)
    : matchConfig_(config), config_(traceConfig) {}

void DecisionTracer::recordStep(const SimMatch::MatchSimulation& simulation) {
  for (const SimMatch::ActionDiagnostic& action : simulation.actionDiagnostics()) {
    entries_.emplace_back(ActionDecisionTrace{.decision = action});
  }
  for (const SimMatch::SweepDiagnostic& sweep : simulation.sweepDiagnostics()) {
    entries_.emplace_back(SweepDecisionTrace{.decision = sweep});
  }
  for (const SimMatch::DuelDiagnostic& duel : simulation.duelDiagnostics()) {
    entries_.emplace_back(DuelDecisionTrace{.decision = duel});
  }
  for (const SimMatch::DiveDiagnostic& dive : simulation.diveDiagnostics()) {
    entries_.emplace_back(DiveDecisionTrace{.decision = dive});
  }
  // Diagnostics before events: the pass-decision system runs before the
  // duel system in the standard pipeline, so a carrier can decide a pass
  // and lose the ball in a duel in the very same step. Opening
  // the new pass here first lets resolve() close it below against that
  // same step's events, instead of missing a loss the next step's
  // diagnostics would otherwise report as an unrelated new decision.
  for (const DecisionDiagnostic& decision : simulation.diagnostics()) {
    if (openPass_) {
      // Only one ball: a new decision means the open pass was never played.
      auto& open = std::get<PassDecisionTrace>(entries_.at(*openPass_));
      open.outcome = settled(PassResult::kNotPlayed, std::nullopt, std::nullopt);
      openPass_.reset();
    }
    entries_.emplace_back(PassDecisionTrace{.decision = decision, .outcome = {}});
    if (decision.outcome == SimMatch::DecisionOutcome::kPassed ||
        decision.outcome == SimMatch::DecisionOutcome::kCrossed) {
      openPass_ = entries_.size() - 1;
      openPassPlayed_ = false;
      openPassPositions_.clear();
      for (const SimMatch::PlayerMatchState& player : simulation.previousState().players()) {
        openPassPositions_.emplace(player.playerId, player.position);
      }
    }
  }
  for (const MatchEvent& event : simulation.events()) {
    resolve(event, simulation.state());
  }
}

void DecisionTracer::resolve(const MatchEvent& event, const SimMatch::MatchState& state) {
  if (!openPass_) {
    return;
  }
  auto& open = std::get<PassDecisionTrace>(entries_.at(*openPass_));
  const SimCore::PlayerId passer = open.decision.player;
  const auto close = [this, &open](PassOutcome outcome) {
    open.outcome = outcome;
    openPass_.reset();
  };
  if (const auto* attempted = std::get_if<SimMatch::PassAttempted>(&event)) {
    openPassPlayed_ = openPassPlayed_ || attempted->passer == passer;
  } else if (const auto* received = std::get_if<SimMatch::PassReceived>(&event);
             received != nullptr && received->passer == passer && openPassPlayed_) {
    close(settled(PassResult::kReceived, received->tick, received->receiver));
  } else if (const auto* intercepted = std::get_if<SimMatch::PassIntercepted>(&event);
             intercepted != nullptr && intercepted->passer == passer && openPassPlayed_) {
    close(attributeInterception(*intercepted, open.decision, openPassPositions_, matchConfig_,
                                config_));
  } else if (const auto* aerial = std::get_if<SimMatch::AerialContest>(&event);
             aerial != nullptr && aerial->winner && openPassPlayed_ && state.ball().lastTouch &&
             state.lastPass().value_or(SimMatch::PassRecord{}).passer == passer) {
    // A cross or a long ball won in the air: by a teammate it reached him,
    // by an opponent it was cut out where they went up for it.
    const auto winner = SimMatch::findPlayerIndex(state, *aerial->winner);
    const auto passerIndex = SimMatch::findPlayerIndex(state, passer);
    if (winner && passerIndex &&
        state.players()[*winner].side == state.players()[*passerIndex].side) {
      close(settled(PassResult::kReceived, aerial->tick, aerial->winner));
    } else {
      close(attributeInterception(SimMatch::PassIntercepted{.tick = aerial->tick,
                                                            .interceptor = *aerial->winner,
                                                            .passer = passer,
                                                            .position = aerial->position},
                                  open.decision, openPassPositions_, matchConfig_, config_));
    }
  } else if (const auto* recovered = std::get_if<SimMatch::LooseBallRecovered>(&event);
             recovered != nullptr && recovered->player == passer && openPassPlayed_) {
    close(settled(PassResult::kRecoveredByPasser, recovered->tick, passer));
  } else if (const auto* change = std::get_if<SimMatch::PossessionChanged>(&event);
             change != nullptr && !openPassPlayed_ && change->previousOwner == passer &&
             change->newOwner) {
    // He lost the ball before the kick.
    close(settled(PassResult::kNotPlayed, change->tick, std::nullopt));
  }
}

PassOutcome attributeInterception(const SimMatch::PassIntercepted& event,
                                  const DecisionDiagnostic& decision,
                                  const TruePositions& truePositions,
                                  const SimMatch::MatchConfig& matchConfig,
                                  const TraceConfig& config) {
  PassOutcome outcome{.result = PassResult::kIntercepted,
                      .tick = event.tick,
                      .by = event.interceptor,
                      .cause = FailureCause::kExecution,
                      .believedMetersOff = std::nullopt};
  const auto observed = std::ranges::find_if(
      decision.observations, [&event, &decision](const SimMatch::Observation& observation) {
        return observation.entity == SimMatch::ObservedEntity::player(event.interceptor) &&
               observation.confidence >= decision.scoring.minConfidence;
      });
  if (observed == decision.observations.end()) {
    outcome.cause = FailureCause::kPerception;
    return outcome;
  }
  if (const auto truth = truePositions.find(event.interceptor); truth != truePositions.end()) {
    const double secondsPerTick = 1.0 / static_cast<double>(matchConfig.ticksPerSecond);
    const double off =
        SimCore::distance(SimMatch::estimatePosition(
                              *observed, {.tick = decision.tick, .secondsPerTick = secondsPerTick},
                              matchConfig.perception),
                          truth->second);
    if (off > config.perceptionErrorMeters) {
      outcome.cause = FailureCause::kPerception;
      outcome.believedMetersOff = off;
      return outcome;
    }
  }
  const std::size_t chosen = decision.chosen.value_or(0);
  if (decision.outcome == SimMatch::DecisionOutcome::kCrossed) {
    // A cross he judged unlikely to reach his side first was his decision.
    if (chosen < decision.crosses.size() &&
        1.0 - decision.crosses.at(chosen).winChance >= config.riskyDecision) {
      outcome.cause = FailureCause::kDecision;
    }
    return outcome;
  }
  if (chosen < decision.candidates.size()) {
    const SimMatch::PassCandidate& pass = decision.candidates.at(chosen);
    if (pass.judgedInterceptionRisk >= config.riskyDecision) {
      outcome.cause = FailureCause::kDecision;
    } else if (pass.interceptionRisk >= config.riskyDecision) {
      outcome.cause = FailureCause::kJudgement;
    }
  }
  return outcome;
}

std::string formatTrace(const std::span<const TraceEntry> entries) {
  std::string text;
  for (const TraceEntry& entry : entries) {
    text += std::visit(
        [](const auto& trace) {
          using Trace = std::decay_t<decltype(trace)>;
          if constexpr (std::is_same_v<Trace, PassDecisionTrace>) {
            return passLine(trace);
          } else if constexpr (std::is_same_v<Trace, ActionDecisionTrace>) {
            return actionLine(trace);
          } else if constexpr (std::is_same_v<Trace, DuelDecisionTrace>) {
            return duelLine(trace);
          } else if constexpr (std::is_same_v<Trace, DiveDecisionTrace>) {
            return diveLine(trace);
          } else {
            return sweepLine(trace);
          }
        },
        entry);
    text += '\n';
  }
  return text;
}

}  // namespace ElyverseFootball::SimReplay
