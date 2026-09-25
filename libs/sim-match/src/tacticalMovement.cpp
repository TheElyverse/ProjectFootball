#include "tacticalMovement.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "choicePolicy.hpp"
#include "matchEvents.hpp"

namespace ElyverseFootball::SimMatch {
namespace {

// Decides an off-ball action for the player at this index and reports it.
[[nodiscard]] PlayerAction decideOffBall(const MatchStepContext& context, const MatchState& current,
                                         const std::size_t index, const DesiredRegion& region,
                                         const TacticalMovementRules& rules) {
  const std::vector<ActionCandidate> candidates =
      generateOffBallCandidates(current, index, region, context.tick(), context.secondsPerTick(),
                                rules.offBall, rules.positioning, rules.perception);
  std::vector<double> utilities;
  utilities.reserve(candidates.size());
  for (const ActionCandidate& candidate : candidates) {
    utilities.push_back(candidate.utility);
  }
  // Holding position is always a candidate, so there is always a choice.
  const std::size_t chosen =
      chooseByUtility(utilities, rules.offBall.temperature,
                      context.random(SimCore::RandomNumberGeneratorDomain::kAi))
          .value_or(0);
  if (context.collectsDiagnostics()) {
    context.diagnose(ActionDiagnostic{.tick = context.tick(),
                                      .player = current.players()[index].playerId,
                                      .candidates = candidates,
                                      .chosen = chosen});
  }
  const ActionCandidate& action = candidates.at(chosen);
  return {.type = action.type,
          .target = action.target,
          .subject = action.subject,
          .decidedAt = context.tick()};
}

}  // namespace

MatchSystem makeTacticalMovementSystem(const TacticalMovementRules& rules) {
  validate(rules.positioning);
  validate(rules.offBall);
  return {.name = std::string(kTacticalMovementSystemName),
          .update =
              [rules](const MatchStepContext& context, const MatchState& current,
                      MatchStateWriter& next) {
                for (std::size_t index = 0; index < current.players().size(); ++index) {
                  const PlayerMatchState& player = current.players()[index];
                  const auto& phase = current.phase(player.side);
                  const bool onTheBall = current.ball().owner == player.playerId;
                  const bool chasing = current.chaser(player.side) == player.playerId;
                  if (!phase || onTheBall || chasing) {
                    continue;
                  }
                  const DesiredRegion region = chooseDesiredRegion(
                      current, index, phase->phase, context.tick(), context.secondsPerTick(),
                      rules.positioning, rules.perception);
                  PlayerTacticalState& tactical = next.tactical(index);
                  tactical.region = region;
                  SimCore::Vec2 target = region.center;
                  if (current.possession().team == player.side) {
                    if (isOffBallDecisionDue(current, index, context.tick(), rules.offBall)) {
                      tactical.action = decideOffBall(context, current, index, region, rules);
                    }
                    if (tactical.action && tactical.action->type != ActionType::kHoldPosition) {
                      target = tactical.action->target;
                    }
                  } else {
                    tactical.action = std::nullopt;
                  }
                  next.setPlayerTarget(index, target);
                }
              },
          .intervalTicks = rules.positioning.intervalTicks,
          .phaseTicks = 0};
}

}  // namespace ElyverseFootball::SimMatch
