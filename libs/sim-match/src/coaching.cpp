#include "coaching.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

#include "configChecks.hpp"
#include "matchEvents.hpp"
#include "matchState.hpp"
#include "tacticHash.hpp"

namespace ElyverseFootball::SimMatch {
namespace {

using ConfigChecks::isFiniteNonNegative;

// The side's tactic on its way: announced in the step it was given, the
// players' from its effective tick on.
void passOnTactic(const MatchStepContext& context, const MatchState& current,
                  MatchStateWriter& next, const TeamSide side, const std::int64_t delay) {
  const auto& pending = current.pendingTactic(side);
  if (!pending) {
    return;
  }
  const SimCore::SimTick effective(pending->given.value() + delay);
  const std::string name(pending->tactic.name());
  const std::uint64_t hash = SimTactics::contentHash(pending->tactic);
  if (pending->given == context.tick()) {
    context.record(InstructionGiven{.tick = context.tick(),
                                    .side = side,
                                    .tactic = name,
                                    .contentHash = hash,
                                    .effective = effective});
  }
  if (context.tick() < effective) {
    return;
  }
  next.setTactic(side, pending->tactic);
  next.setPendingTactic(side, std::nullopt);
  context.record(TacticChanged{.tick = context.tick(),
                               .side = side,
                               .tactic = name,
                               .contentHash = hash,
                               .given = pending->given});
}

// Why the substitution cannot be made now, if it cannot.
[[nodiscard]] std::optional<SubstitutionRefusal> refusalOf(
    const Substitution& substitution, std::span<const PlayerMatchState> players, const int made,
    const int limit) {
  const auto playing = [players](const SimCore::PlayerId playerId) {
    return std::ranges::find(players, playerId, &PlayerMatchState::playerId);
  };
  const auto off = playing(substitution.off);
  if (off == players.end() || off->side != substitution.side || !isOnPitch(*off)) {
    return SubstitutionRefusal::kPlayerOff;
  }
  if (playing(substitution.on) != players.end()) {
    return SubstitutionRefusal::kPlayerUsed;
  }
  if (made >= limit) {
    return SubstitutionRefusal::kLimitReached;
  }
  return std::nullopt;
}

// Every substitution ordered so far, in order, now that the ball is dead.
void substitute(const MatchStepContext& context, const MatchState& current, MatchStateWriter& next,
                const int limit) {
  // Home, away; and who has come off, before or in this stoppage.
  std::array<int, 2> made{current.substitutionsMade(TeamSide::kHome),
                          current.substitutionsMade(TeamSide::kAway)};
  std::vector<SimCore::PlayerId> cameOff;
  for (const Substitution& earlier : current.substitutions()) {
    cameOff.push_back(earlier.off);
  }
  for (const Substitution& substitution : current.pendingSubstitutions()) {
    int& count = made.at(substitution.side == TeamSide::kHome ? 0 : 1);
    auto refusal = refusalOf(substitution, next.players(), count, limit);
    if (!refusal && std::ranges::find(cameOff, substitution.on) != cameOff.end()) {
      refusal = SubstitutionRefusal::kPlayerUsed;
    }
    if (refusal) {
      context.record(SubstitutionRefused{.tick = context.tick(),
                                         .side = substitution.side,
                                         .off = substitution.off,
                                         .on = substitution.on,
                                         .ordered = substitution.ordered,
                                         .reason = *refusal});
      continue;
    }
    next.substitute(substitution);
    ++count;
    cameOff.push_back(substitution.off);
    context.record(SubstitutionMade{.tick = context.tick(),
                                    .side = substitution.side,
                                    .off = substitution.off,
                                    .on = substitution.on,
                                    .ordered = substitution.ordered});
  }
  next.setPendingSubstitutions({});
}

}  // namespace

void validate(const CoachingConfig& config) {
  if (!isFiniteNonNegative(config.instructionDelaySeconds) ||
      config.instructionDelaySeconds > kMaxInstructionDelaySeconds || config.maxSubstitutions < 0 ||
      config.maxSubstitutions > kMaxSubstitutions) {
    throw std::invalid_argument("coaching: invalid configuration");
  }
}

std::int64_t instructionDelayTicks(const CoachingConfig& config, const int ticksPerSecond) {
  // In whole milliseconds, so 8.3 s at 30 Hz is 249 ticks, not 250.
  const auto milliseconds = std::llround(config.instructionDelaySeconds * 1000.0);
  return ((milliseconds * ticksPerSecond) + 999) / 1000;
}

MatchSystem makeCoachingSystem(const CoachingRules& rules) {
  validate(rules.coaching);
  const std::int64_t delay = instructionDelayTicks(rules.coaching, rules.ticksPerSecond);
  return {.name = std::string(kCoachingSystemName),
          .update =
              [rules, delay](const MatchStepContext& context, const MatchState& current,
                             MatchStateWriter& next) {
                for (const TeamSide side : {TeamSide::kHome, TeamSide::kAway}) {
                  passOnTactic(context, current, next, side, delay);
                }
                // Substitutions wait for a stoppage: a set piece being set up.
                if (!current.pendingSubstitutions().empty() && next.setPiece()) {
                  substitute(context, current, next, rules.coaching.maxSubstitutions);
                }
              },
          .intervalTicks = 1,
          .phaseTicks = 0};
}

}  // namespace ElyverseFootball::SimMatch
