#include "fouls.hpp"

#include <cstddef>
#include <optional>
#include <stdexcept>
#include <string>

#include "configChecks.hpp"
#include "restart.hpp"
#include "teamFrame.hpp"
#include "zones.hpp"

namespace ElyverseFootball::SimMatch {
namespace {

// How far beside the touchline a player sent off stands.
inline constexpr double kBenchDistance = 5.0;  // m

// Whether no outfield player of the offender's side but the offender is nearer
// the goal the victim attacks than the victim, on the pitch.
[[nodiscard]] bool lastDefender(const MatchState& state, const PlayerMatchState& victim,
                                const SimCore::PlayerId offender) {
  const TeamSide attacking = victim.side;
  const double depth = depthOf(attacking, victim.position, state.pitch());
  for (std::size_t index = 0; index < state.players().size(); ++index) {
    const PlayerMatchState& player = state.players()[index];
    if (player.playerId != offender && player.side != attacking && isOnPitch(player) &&
        !keepsGoal(state, index) && depthOf(attacking, player.position, state.pitch()) > depth) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] Card cardOf(const bool denyingGoal, const bool inArea, const bool fromBehind,
                          const int yellowCards) noexcept {
  if (denyingGoal && !inArea) {
    return Card::kRed;
  }
  if (denyingGoal || fromBehind) {
    return yellowCards > 0 ? Card::kSecondYellow : Card::kYellow;
  }
  return Card::kNone;
}

}  // namespace

void validate(const FoulConfig& config) {
  const bool valid = ConfigChecks::isWithin(config.fromBehindDot, -1.0, 1.0) &&
                     ConfigChecks::isPositiveFinite(config.denyingDistance) &&
                     ConfigChecks::isShare(config.advantageDepth);
  if (!valid) {
    throw std::invalid_argument("fouls: invalid configuration");
  }
}

FoulJudgement judgeFoul(const MatchState& state, const FoulRecord& foul, const FoulConfig& config) {
  const auto offender = findPlayerIndex(state, foul.offender);
  const auto victim = findPlayerIndex(state, foul.victim);
  if (!offender || !victim) {
    return {};
  }
  const PlayerMatchState& tackler = state.players()[*offender];
  const PlayerMatchState& fouled = state.players()[*victim];
  const Pitch& pitch = state.pitch();
  const TeamSide attacking = fouled.side;

  FoulJudgement judgement;
  judgement.fromBehind = fouled.facing.dot(SimCore::unitOr(tackler.position - fouled.position,
                                                           fouled.facing)) < config.fromBehindDot;
  judgement.denyingGoal = SimCore::distance(foul.position, attackedGoal(pitch, attacking).center) <=
                              config.denyingDistance &&
                          lastDefender(state, fouled, foul.offender);
  const bool inArea = pitch.penaltyArea(ownGoalEnd(tackler.side)).contains(foul.position);
  judgement.card = cardOf(judgement.denyingGoal, inArea, judgement.fromBehind, tackler.yellowCards);

  const bool sentOff = judgement.card == Card::kRed || judgement.card == Card::kSecondYellow;
  const auto owner =
      state.ball().owner ? findPlayerIndex(state, *state.ball().owner) : std::nullopt;
  const bool kept = owner && state.players()[*owner].side == attacking;
  if (inArea) {
    judgement.restart = FoulRestart::kPenalty;
  } else if (kept && !sentOff &&
             depthOf(attacking, foul.position, pitch) >=
                 config.advantageDepth * pitch.lengthMeters()) {
    judgement.restart = FoulRestart::kAdvantage;
  }
  return judgement;
}

SimCore::Vec2 benchPosition(const Pitch& pitch) noexcept {
  return {.x = pitch.lengthMeters() / 2.0, .y = -kBenchDistance};
}

MatchSystem makeFoulSystem(const FoulRules& rules) {
  validate(rules.fouls);
  validate(rules.physics);
  return {
      .name = std::string(kFoulSystemName),
      .update =
          [rules](const MatchStepContext& context, const MatchState& current,
                  MatchStateWriter& next) {
            const auto& foul = next.lastFoul();
            if (!foul || foul->tick != context.tick()) {
              return;
            }
            const auto offender = findPlayerIndex(current, foul->offender);
            const auto victim = findPlayerIndex(current, foul->victim);
            if (!offender || !victim) {
              return;
            }
            const FoulJudgement judgement = judgeFoul(current, *foul, rules.fouls);
            const TeamSide attacking = current.players()[*victim].side;
            context.record(FoulCalled{.tick = context.tick(),
                                      .side = current.players()[*offender].side,
                                      .offender = foul->offender,
                                      .victim = foul->victim,
                                      .position = foul->position,
                                      .restart = judgement.restart,
                                      .card = judgement.card,
                                      .fromBehind = judgement.fromBehind,
                                      .denyingGoal = judgement.denyingGoal});
            if (judgement.card != Card::kNone) {
              next.addLostSeconds(rules.clock.cardLostSeconds);
            }
            if (judgement.card == Card::kYellow || judgement.card == Card::kSecondYellow) {
              next.addYellowCard(*offender);
            }
            if (judgement.card == Card::kRed || judgement.card == Card::kSecondYellow) {
              next.sendOff(*offender, benchPosition(current.pitch()));
            }
            if (judgement.restart == FoulRestart::kAdvantage) {
              return;
            }
            const bool penalty = judgement.restart == FoulRestart::kPenalty;
            next.setOffside(std::nullopt);
            next.setOffsideExemption(std::nullopt);
            (void)awardFreeKick(
                context, current, next,
                {.kind = penalty ? RestartKind::kPenalty : RestartKind::kDirectFreeKick,
                 .side = attacking,
                 .spot = penalty ? current.pitch().penaltySpot(ownGoalEnd(opponentOf(attacking)))
                                 : foul->position},
                rules.physics, rules.clock);
          },
      .intervalTicks = 1,
      .phaseTicks = 0};
}

}  // namespace ElyverseFootball::SimMatch
