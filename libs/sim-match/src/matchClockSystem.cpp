#include "matchClockSystem.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

#include "matchEvents.hpp"
#include "restart.hpp"

namespace ElyverseFootball::SimMatch {
namespace {

// The second half's kickoff: both sides lined up, the ball at the taker's
// feet, a set piece when the match takes restarts. Nothing without a taker.
void kickOffSecondHalf(const MatchStepContext& context, const MatchState& current,
                       MatchStateWriter& next, const MatchClockRules& rules) {
  if (const auto lineUp = lineUpForKickoff(current, TeamSide::kAway, rules.physics)) {
    kickOff(context, current, next, *lineUp, rules.setPieces);
  }
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
                  // Off the pitch nobody tires.
                  if (!isOnPitch(player)) {
                    ++index;
                    continue;
                  }
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
                // A foul's sending-off settles before the line-up (docs/fouls.md#cards).
                const bool foulNow = next.lastFoul() && next.lastFoul()->tick == context.tick();
                // A half is extended for a penalty to be taken (Law 7).
                const bool penaltyDue =
                    next.setPiece() && next.setPiece()->kind == SimTactics::SetPieceKind::kPenalty;
                if (!timeUp || shotOnItsWay || foulNow || penaltyDue) {
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
                kickOffSecondHalf(context, current, next, rules);
              },
          .intervalTicks = 1,
          .phaseTicks = 0};
}

}  // namespace ElyverseFootball::SimMatch
