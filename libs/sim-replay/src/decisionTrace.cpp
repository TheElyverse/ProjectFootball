#include "decisionTrace.hpp"

#include <cstddef>
#include <format>
#include <string>
#include <utility>
#include <variant>

#include "actionCandidate.hpp"
#include "observation.hpp"
#include "passCandidates.hpp"
#include "perception.hpp"
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

[[nodiscard]] std::string signedNumber(const double value) {
  return std::format("{:+.2f}", value);
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

[[nodiscard]] std::string passLine(const PassDecisionTrace& trace) {
  const DecisionDiagnostic& decision = trace.decision;
  std::string line = std::format("t={} #{} ", decision.tick.value(), decision.player.value());
  const std::string context = std::format("{} options, {} observed", decision.candidates.size(),
                                          decision.observations.size());
  if (!decision.chosen || *decision.chosen >= decision.candidates.size()) {
    return line + std::format("keeps the ball: no valid option ({})", context);
  }
  const SimMatch::PassCandidate& pass = decision.candidates.at(*decision.chosen);
  const SimMatch::PassContributions parts = SimMatch::passContributions(pass, decision.scoring);
  line += std::format(
      "passes to #{} (utility {:.2f}: completion {} progression {} pressure {} risk {}; "
      "estimated risk {:.2f}; {}) because {}",
      pass.receiver.value(), pass.utility, signedNumber(parts.completion),
      signedNumber(parts.progression), signedNumber(parts.pressure), signedNumber(parts.risk),
      pass.interceptionRisk, context, SimMatch::dominantContribution(parts));
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
  return line + std::format(
                    " (utility {:.2f}: responsibility {} region {} space {} lane {} urgency {} "
                    "effort {}; {} options) because {}",
                    action.utility, signedNumber(scores.responsibility),
                    signedNumber(scores.region), signedNumber(scores.space),
                    signedNumber(scores.lane), signedNumber(scores.urgency),
                    signedNumber(scores.effort), decision.candidates.size(),
                    SimMatch::dominantScore(scores));
}

}  // namespace

std::string_view failureCauseName(const FailureCause cause) noexcept {
  switch (cause) {
    case FailureCause::kPerception:
      return "perception";
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
  for (const MatchEvent& event : simulation.events()) {
    resolve(event);
  }
  for (const SimMatch::ActionDiagnostic& action : simulation.actionDiagnostics()) {
    entries_.emplace_back(ActionDecisionTrace{.decision = action});
  }
  for (const DecisionDiagnostic& decision : simulation.diagnostics()) {
    if (openPass_) {
      // Only one ball: a new decision means the open pass was never played.
      auto& open = std::get<PassDecisionTrace>(entries_.at(*openPass_));
      open.outcome = settled(PassResult::kNotPlayed, std::nullopt, std::nullopt);
      openPass_.reset();
    }
    entries_.emplace_back(PassDecisionTrace{.decision = decision, .outcome = {}});
    if (decision.outcome == SimMatch::DecisionOutcome::kPassed) {
      openPass_ = entries_.size() - 1;
      openPassPlayed_ = false;
      openPassPositions_.clear();
      for (const SimMatch::PlayerMatchState& player : simulation.previousState().players()) {
        openPassPositions_.emplace(player.playerId, player.position);
      }
    }
  }
}

void DecisionTracer::resolve(const MatchEvent& event) {
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
        SimCore::distance(SimMatch::estimatePosition(*observed, decision.tick, secondsPerTick,
                                                     matchConfig.perception),
                          truth->second);
    if (off > config.perceptionErrorMeters) {
      outcome.cause = FailureCause::kPerception;
      outcome.believedMetersOff = off;
      return outcome;
    }
  }
  const std::size_t chosen = decision.chosen.value_or(0);
  if (chosen < decision.candidates.size() &&
      decision.candidates.at(chosen).interceptionRisk >= config.riskyDecision) {
    outcome.cause = FailureCause::kDecision;
  }
  return outcome;
}

std::string formatTrace(const std::span<const TraceEntry> entries) {
  std::string text;
  for (const TraceEntry& entry : entries) {
    text += std::visit(
        [](const auto& trace) {
          if constexpr (std::is_same_v<std::decay_t<decltype(trace)>, PassDecisionTrace>) {
            return passLine(trace);
          } else {
            return actionLine(trace);
          }
        },
        entry);
    text += '\n';
  }
  return text;
}

}  // namespace ElyverseFootball::SimReplay
