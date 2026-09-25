#include "tacticalPhases.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <string>

#include "teamFrame.hpp"

namespace ElyverseFootball::SimMatch {
namespace {

using SimTactics::TacticalPhase;

void validate(const PhaseConfig& config) {
  constexpr double kMax = std::numeric_limits<double>::max();
  const bool valid = config.intervalTicks >= 1 && config.transitionSeconds >= 0.0 &&
                     config.transitionSeconds <= kMax && config.hysteresisMeters >= 0.0 &&
                     config.hysteresisMeters <= kMax;
  if (!valid) {
    throw std::invalid_argument("tactical phase: invalid configuration");
  }
}

constexpr std::array<TacticalPhase, 3> kThirds{TacticalPhase::kBuildUp, TacticalPhase::kProgression,
                                               TacticalPhase::kFinalThird};

// The third a depth lies in: 0, 1 or 2.
[[nodiscard]] std::size_t thirdOf(const double depth, const double length) noexcept {
  if (depth < length / 3.0) {
    return 0;
  }
  return depth < 2.0 * length / 3.0 ? 1 : 2;
}

[[nodiscard]] std::optional<std::size_t> thirdIndex(const TacticalPhase phase) noexcept {
  for (std::size_t index = 0; index < kThirds.size(); ++index) {
    if (kThirds.at(index) == phase) {
      return index;
    }
  }
  return std::nullopt;
}

// The third with the ball, keeping the previous third while the ball is
// within the margin of the boundary it would cross into a neighbouring one.
[[nodiscard]] TacticalPhase attackingThird(const PhaseSituation& situation,
                                           const PhaseConfig& config) noexcept {
  const double length = situation.pitchLength;
  const std::size_t raw = thirdOf(situation.ballDepth, length);
  if (situation.previous) {
    if (const auto previous = thirdIndex(*situation.previous)) {
      const std::size_t higher = raw > *previous ? raw : *previous;
      const bool neighbours = raw + 1 == *previous || *previous + 1 == raw;
      const double boundary = static_cast<double>(higher) * length / 3.0;
      if (neighbours && std::abs(situation.ballDepth - boundary) < config.hysteresisMeters) {
        return kThirds.at(*previous);
      }
    }
  }
  return kThirds.at(raw);
}

// Pressing or block, with the same margin around the pressing line.
[[nodiscard]] TacticalPhase defendingPhase(const PhaseSituation& situation,
                                           const PhaseConfig& config) noexcept {
  const double line = situation.tactic->principles().pressingLine * situation.pitchLength;
  double threshold = line;
  if (situation.previous == TacticalPhase::kPressing) {
    threshold -= config.hysteresisMeters;
  } else if (situation.previous == TacticalPhase::kDefensiveBlock) {
    threshold += config.hysteresisMeters;
  }
  return situation.ballDepth >= threshold ? TacticalPhase::kPressing
                                          : TacticalPhase::kDefensiveBlock;
}

}  // namespace

std::optional<TeamSide> teamOnTheBall(const MatchState& state) noexcept {
  const BallState& ball = state.ball();
  std::optional<SimCore::PlayerId> player = ball.owner;
  if (!player && ball.lastTouch) {
    player = ball.lastTouch->playerId;
  }
  if (!player) {
    return std::nullopt;
  }
  if (const auto index = findPlayerIndex(state, *player)) {
    return state.players()[*index].side;
  }
  return std::nullopt;
}

TeamPossession updatePossession(const MatchState& state, const TeamPossession& possession,
                                const SimCore::SimTick now) noexcept {
  const std::optional<TeamSide> team = teamOnTheBall(state);
  if (!team || team == possession.team) {
    return possession;
  }
  const auto& touch = state.ball().lastTouch;
  return {.team = team,
          .since = touch ? touch->tick : now,
          .fromOpponent = possession.team.has_value()};
}

TacticalPhase classifyPhase(const PhaseSituation& situation, const PhaseConfig& config) noexcept {
  const TeamPossession& possession = situation.possession;
  const bool inTransition =
      possession.fromOpponent && situation.possessionSeconds < config.transitionSeconds;
  if (possession.team == situation.side) {
    return inTransition ? TacticalPhase::kAttackingTransition : attackingThird(situation, config);
  }
  if (possession.team && inTransition) {
    return TacticalPhase::kDefensiveTransition;
  }
  return defendingPhase(situation, config);
}

MatchSystem makePhaseSystem(const PhaseConfig& config) {
  validate(config);
  return {.name = std::string(kPhaseSystemName),
          .update =
              [config](const MatchStepContext& context, const MatchState& current,
                       MatchStateWriter& next) {
                const TeamPossession possession =
                    updatePossession(current, current.possession(), context.tick());
                next.setPossession(possession);
                const double possessionSeconds =
                    static_cast<double>(context.tick().value() - possession.since.value()) *
                    context.secondsPerTick();
                for (const TeamSide side : {TeamSide::kHome, TeamSide::kAway}) {
                  const auto& tactic = current.tactics().of(side);
                  if (!tactic) {
                    continue;
                  }
                  const auto& previous = current.phase(side);
                  const PhaseSituation situation{
                      .side = side,
                      .tactic = &*tactic,
                      .possession = possession,
                      .ballDepth = depthOf(side, current.ball().position, current.pitch()),
                      .pitchLength = current.pitch().lengthMeters(),
                      .possessionSeconds = possessionSeconds,
                      .previous = previous ? std::optional(previous->phase) : std::nullopt};
                  const TacticalPhase phase = classifyPhase(situation, config);
                  if (previous && previous->phase == phase) {
                    continue;
                  }
                  next.setPhase(side, TeamPhase{.phase = phase, .since = context.tick()});
                  context.record(PhaseChanged{
                      .tick = context.tick(),
                      .side = side,
                      .previous = previous ? std::optional(previous->phase) : std::nullopt,
                      .phase = phase});
                }
              },
          .intervalTicks = config.intervalTicks,
          .phaseTicks = 0};
}

}  // namespace ElyverseFootball::SimMatch
