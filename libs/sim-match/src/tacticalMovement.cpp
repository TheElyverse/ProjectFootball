#include "tacticalMovement.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <optional>
#include <string>
#include <vector>

#include "choicePolicy.hpp"
#include "matchEvents.hpp"
#include "pressingActions.hpp"
#include "tacticalPhases.hpp"
#include "teamFrame.hpp"
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
  if (context.collectsDiagnostics(current.players()[index].playerId)) {
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

// The action a player's role in his side's press gives him, aimed at where
// the players stand now; empty if he has no role. The team assigned it, so
// he does not choose: the diagnostic says so and holds that one action.
[[nodiscard]] std::optional<PlayerAction> pressRoleAction(const MatchStepContext& context,
                                                          const MatchState& current,
                                                          const std::size_t index,
                                                          const TacticalMovementRules& rules) {
  const PlayerMatchState& player = current.players()[index];
  const auto& press = current.press(player.side);
  if (!press) {
    return std::nullopt;
  }
  const auto assignment =
      std::ranges::find(press->assignments, player.playerId, &PressAssignment::player);
  const auto subject = assignment != press->assignments.end()
                           ? findPlayerIndex(current, assignment->subject)
                           : std::nullopt;
  const auto carrier = findPlayerIndex(current, press->carrier);
  if (!subject || !carrier) {
    return std::nullopt;
  }
  const Pitch& pitch = current.pitch();
  const SimCore::Vec2 ownGoal{.x = xAtDepth(player.side, 0.0, pitch),
                              .y = pitch.widthMeters() / 2.0};
  const SimCore::Vec2 carrierAt = current.players()[*carrier].position;
  const SimCore::Vec2 subjectAt = current.players()[*subject].position;
  ActionCandidate action{.type = ActionType::kPressCarrier,
                         .target = {},
                         .subject = assignment->subject,
                         .scores = {},
                         .utility = 0.0};
  switch (assignment->role) {
    case PressRole::kPress: {
      // Cut off the option the lane blockers do not cover: the nearest one
      // not blocked, else goal-side.
      std::optional<SimCore::Vec2> option;
      double nearest = std::numeric_limits<double>::infinity();
      for (const PlayerMatchState& other : current.players()) {
        const bool blocked = std::ranges::any_of(press->assignments, [&](const auto& role) {
          return role.role == PressRole::kBlockLane && role.subject == other.playerId;
        });
        const double distance = std::sqrt((other.position - carrierAt).lengthSquared());
        if (other.side != player.side && other.playerId != press->carrier && !blocked &&
            distance < nearest) {
          option = other.position;
          nearest = distance;
        }
      }
      action.target = pressTarget(carrierAt, option, ownGoal, rules.defensive.pressDistance);
      break;
    }
    case PressRole::kBlockLane:
      action.type = ActionType::kBlockLane;
      action.target = laneBlockTarget({.carrier = carrierAt, .receiver = subjectAt},
                                      player.position, rules.defensive.laneMinDistance);
      break;
    case PressRole::kCover:
      action.type = ActionType::kCover;
      action.target = coverTarget(subjectAt, ownGoal, rules.defensive.coverDistance);
      break;
  }
  action.target = pitch.clamp(action.target);
  if (context.collectsDiagnostics(player.playerId)) {
    context.diagnose(ActionDiagnostic{.tick = context.tick(),
                                      .player = player.playerId,
                                      .candidates = {action},
                                      .chosen = 0,
                                      .assigned = true});
  }
  return PlayerAction{.type = action.type,
                      .target = action.target,
                      .subject = action.subject,
                      .decidedAt = context.tick(),
                      .withBall = false};
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
                  if (const auto assigned = pressRoleAction(context, current, index, rules)) {
                    tactical.action = assigned;
                    target = assigned->target;
                  } else if (!isGoalkeeper(current, index)) {
                    // Not possession().team: it only updates when the phase
                    // system next runs, so it can still name the losing side
                    // right after a turnover the ball itself already shows.
                    const bool withBall = teamOnTheBall(current) == player.side;
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
