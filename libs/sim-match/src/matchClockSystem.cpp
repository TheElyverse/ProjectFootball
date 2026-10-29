#include "matchClockSystem.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

#include "matchEvents.hpp"
#include "restart.hpp"
#include "teamFrame.hpp"

namespace ElyverseFootball::SimMatch {
namespace {

// The second half's kickoff: both sides lined up, the ball at the taker's
// feet. Nothing without a taker.
void kickOffSecondHalf(const MatchStepContext& context, const MatchState& current,
                       MatchStateWriter& next, const TeamSide kicking, const BallPhysics& physics) {
  const auto lineUp = lineUpForKickoff(current, kicking, physics);
  if (!lineUp) {
    return;
  }
  const SimCore::PlayerId taker = current.players()[lineUp->takerIndex].playerId;
  if (next.ball().owner != taker) {
    context.record(PossessionChanged{
        .tick = context.tick(), .previousOwner = next.ball().owner, .newOwner = taker});
  }
  next.setBallOwner(taker);
  next.setBallHeld(false);
  next.setBallTouch(std::nullopt);
  next.setBallLastTouch(BallTouch{.playerId = taker, .tick = context.tick()});
  next.setBallVelocity({});
  next.setBallHeight(0.0);
  next.setBallVerticalVelocity(0.0);
  next.setBallSpin(0.0);
  next.setBallSideSpin(0.0);
  takeKickoffLineUp(current, *lineUp, next);
}

}  // namespace

MatchSystem makeMatchClockSystem(const MatchClockRules& rules) {
  validate(rules.clock, rules.ticksPerSecond);
  validate(rules.fitness);
  validate(rules.physics);
  const std::int64_t half = halfTicks(rules.clock, rules.ticksPerSecond);
  return {.name = std::string(kMatchClockSystemName),
          .update =
              [rules, half](const MatchStepContext& context, const MatchState& current,
                            MatchStateWriter& next) {
                for (std::size_t index = 0; const PlayerMatchState& player : current.players()) {
                  next.setPlayerFitness(
                      index,
                      stepFitness(player,
                                  {.secondsPerTick = context.secondsPerTick(), .halfTicks = half},
                                  rules.fitness));
                  ++index;
                }

                MatchClock clock = next.clock();
                const std::int64_t elapsed = context.tick().value() - clock.periodStart.value();
                if (!clock.addedMinutes && elapsed >= half) {
                  clock.addedMinutes = addedMinutes(clock.lostSeconds, rules.clock);
                  next.setClock(clock);
                }
                const bool timeUp = clock.addedMinutes &&
                                    elapsed >= half + addedTicks(*clock.addedMinutes, rules.clock,
                                                                 rules.ticksPerSecond);
                const bool shotOnItsWay = next.lastShot() && !next.lastShot()->resolved;
                if (!timeUp || shotOnItsWay) {
                  return;
                }
                context.record(PeriodEnded{.tick = context.tick(),
                                           .period = clock.period,
                                           .addedMinutes = clock.addedMinutes.value_or(0)});
                if (clock.period == MatchPeriod::kSecondHalf) {
                  clock.period = MatchPeriod::kFullTime;
                  next.setClock(clock);
                  return;
                }
                next.setClock(MatchClock{.period = MatchPeriod::kSecondHalf,
                                         .periodStart = context.tick(),
                                         .lostSeconds = 0.0,
                                         .addedMinutes = std::nullopt});
                kickOffSecondHalf(context, current, next, opponentOf(rules.clock.openingKickoff),
                                  rules.physics);
              },
          .intervalTicks = 1,
          .phaseTicks = 0};
}

}  // namespace ElyverseFootball::SimMatch
