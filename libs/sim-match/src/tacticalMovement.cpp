#include "tacticalMovement.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "choicePolicy.hpp"
#include "matchEvents.hpp"
#include "zones.hpp"

namespace ElyverseFootball::SimMatch {
namespace {

// Chooses one of the candidates with the seeded softmax, reports the
// decision, and returns it as the player's action. Holding position is
// always the first candidate, so there is always a choice.
[[nodiscard]] PlayerAction decide(const MatchStepContext& context, const MatchState& current,
                                  const std::size_t index,
                                  const std::vector<ActionCandidate>& candidates,
                                  const double temperature, const bool withBall) {
  std::vector<double> utilities;
  utilities.reserve(candidates.size());
  for (const ActionCandidate& candidate : candidates) {
    utilities.push_back(candidate.utility);
  }
  const std::size_t chosen =
      chooseByUtility(utilities, temperature,
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
          .decidedAt = context.tick(),
          .withBall = withBall};
}

// The candidates of a player with or without the ball.
[[nodiscard]] std::vector<ActionCandidate> candidatesFor(
    const MatchStepContext& context, const MatchState& current, const std::size_t index,
    const DesiredRegion& region, const TacticalMovementRules& rules, const bool withBall) {
  if (withBall) {
    return generateOffBallCandidates(current, index, region, context.tick(),
                                     context.secondsPerTick(), rules.offBall, rules.positioning,
                                     rules.perception);
  }
  return generateDefensiveCandidates(current, index, region,
                                     {.now = context.tick(),
                                      .secondsPerTick = context.secondsPerTick(),
                                      .config = &rules.defensive,
                                      .positioning = &rules.positioning,
                                      .perception = &rules.perception});
}

}  // namespace

MatchSystem makeTacticalMovementSystem(const TacticalMovementRules& rules) {
  validate(rules.positioning);
  validate(rules.offBall);
  validate(rules.defensive);
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
                  if (!isGoalkeeper(current, index)) {
                    const bool withBall = current.possession().team == player.side;
                    if (isActionDecisionDue(current, index, context.tick(), rules.offBall)) {
                      const auto candidates =
                          candidatesFor(context, current, index, region, rules, withBall);
                      const double temperature =
                          withBall ? rules.offBall.temperature : rules.defensive.temperature;
                      tactical.action =
                          decide(context, current, index, candidates, temperature, withBall);
                    }
                    if (tactical.action && tactical.action->type != ActionType::kHoldPosition) {
                      target = tactical.action->target;
                    }
                  }
                  next.setPlayerTarget(index, target);
                }
              },
          .intervalTicks = rules.positioning.intervalTicks,
          .phaseTicks = 0};
}

}  // namespace ElyverseFootball::SimMatch
