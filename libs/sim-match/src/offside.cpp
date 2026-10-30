#include "offside.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>

#include "configChecks.hpp"
#include "matchEvents.hpp"
#include "restart.hpp"
#include "teamFrame.hpp"

namespace ElyverseFootball::SimMatch {
namespace {

// One bit per player index: more than any squad has.
inline constexpr std::size_t kMaxWatchedPlayers = 64;

[[nodiscard]] bool watches(const OffsideWatch& watch, const std::size_t index) noexcept {
  return index < kMaxWatchedPlayers && ((watch.offsidePlayers >> index) & 1U) != 0U;
}

// Who played the ball in this step with a pass or a shot.
[[nodiscard]] std::optional<SimCore::PlayerId> playedNow(const MatchStateWriter& next,
                                                         const SimCore::SimTick now) {
  if (const auto& pass = next.lastPass(); pass && pass->tick == now) {
    return pass->passer;
  }
  if (const auto& shot = next.lastShot(); shot && shot->tick == now) {
    return shot->shooter;
  }
  return std::nullopt;
}

// The play `passer` makes from the state before the step, empty if nobody of
// his side stands in an offside position.
[[nodiscard]] std::optional<OffsideWatch> watchOf(const MatchState& current,
                                                  const std::size_t passer,
                                                  const SimCore::SimTick now,
                                                  const OffsideConfig& config) {
  const TeamSide side = current.players()[passer].side;
  std::vector<SimCore::Vec2> opponents;
  for (const PlayerMatchState& player : current.players()) {
    if (player.side != side && isOnPitch(player)) {
      opponents.push_back(player.position);
    }
  }
  const Pitch& pitch = current.pitch();
  OffsideWatch watch{.side = side,
                     .passer = current.players()[passer].playerId,
                     .tick = now,
                     .line = offsideLine(side, opponents, current.ball().position, pitch),
                     .offsidePlayers = 0};
  for (std::size_t index = 0; index < current.players().size() && index < kMaxWatchedPlayers;
       ++index) {
    const PlayerMatchState& player = current.players()[index];
    if (index != passer && player.side == side &&
        isOffsidePosition(side, player.position, watch.line, pitch, config)) {
      watch.offsidePlayers |= std::uint64_t{1} << index;
    }
  }
  if (watch.offsidePlayers == 0) {
    return std::nullopt;
  }
  return watch;
}

// Whether the touch only deflected or parried a shot, or deflected a pass at
// full stretch: no deliberate play.
[[nodiscard]] bool deflects(const MatchStateWriter& next, const BallTouch& touch) {
  const auto& shot = next.lastShot();
  const auto& pass = next.lastPass();
  return (shot && (shot->deflection == touch || shot->parry == touch)) ||
         (pass && pass->deflection == touch);
}

}  // namespace

void validate(const OffsideConfig& config) {
  if (!ConfigChecks::isFiniteNonNegative(config.levelMeters)) {
    throw std::invalid_argument("offside: invalid configuration");
  }
}

double offsideLine(const TeamSide attacking, const std::span<const SimCore::Vec2> opponents,
                   const SimCore::Vec2 ball, const Pitch& pitch) {
  std::vector<double> depths;
  depths.reserve(opponents.size());
  for (const SimCore::Vec2 opponent : opponents) {
    depths.push_back(depthOf(attacking, opponent, pitch));
  }
  std::ranges::sort(depths, std::greater<>());
  const double secondLast = depths.size() >= 2 ? depths[1] : pitch.lengthMeters();
  return std::max({secondLast, depthOf(attacking, ball, pitch), pitch.lengthMeters() / 2.0});
}

bool isOffsidePosition(const TeamSide side, const SimCore::Vec2 position, const double line,
                       const Pitch& pitch, const OffsideConfig& config) noexcept {
  return depthOf(side, position, pitch) > line + config.levelMeters;
}

double believedOffsideLine(const TeamSide side,
                           const std::span<const SimCore::Vec2> rememberedOpponents,
                           const SimCore::Vec2 ball, const Pitch& pitch) {
  if (rememberedOpponents.size() < 2) {
    return std::max(depthOf(side, ball, pitch), pitch.lengthMeters() / 2.0);
  }
  return offsideLine(side, rememberedOpponents, ball, pitch);
}

double believedOffsideLine(const TeamSide side, const std::vector<RememberedPlayer>& remembered,
                           const SimCore::Vec2 ball, const Pitch& pitch) {
  std::vector<SimCore::Vec2> opponents;
  for (const RememberedPlayer& other : remembered) {
    if (!other.teammate) {
      opponents.push_back(other.position);
    }
  }
  return believedOffsideLine(side, opponents, ball, pitch);
}

MatchSystem makeOffsideSystem(const OffsideRules& rules) {
  validate(rules.offside);
  validate(rules.physics);
  return {.name = std::string(kOffsideSystemName),
          .update =
              [rules](const MatchStepContext& context, const MatchState& current,
                      MatchStateWriter& next) {
                const auto& touch = next.ball().lastTouch;
                if (const auto& watch = current.offside();
                    watch && touch && touch != current.ball().lastTouch) {
                  const auto index = findPlayerIndex(current, touch->playerId);
                  if (index && watches(*watch, *index)) {
                    const SimCore::Vec2 position = current.players()[*index].position;
                    context.record(
                        OffsideCalled{.tick = context.tick(),
                                      .side = watch->side,
                                      .player = touch->playerId,
                                      .passer = watch->passer,
                                      .lineX = xAtDepth(watch->side, watch->line, current.pitch()),
                                      .position = position});
                    next.setOffside(std::nullopt);
                    next.setOffsideExemption(std::nullopt);
                    (void)awardFreeKick(context, current, next,
                                        {.kind = RestartKind::kIndirectFreeKick,
                                         .side = opponentOf(watch->side),
                                         .spot = position},
                                        rules.clock);
                    return;
                  }
                  const bool teammate = index && current.players()[*index].side == watch->side;
                  if (teammate || !deflects(next, *touch)) {
                    next.setOffside(std::nullopt);
                  }
                }
                if (const auto played = playedNow(next, context.tick())) {
                  const auto passer = findPlayerIndex(current, *played);
                  // The first play from a throw-in, a goal kick or a corner: the
                  // restart's touch, or the set piece's in this very step.
                  const bool exempt =
                      (current.ball().lastTouch &&
                       current.ball().lastTouch == current.offsideExemption()) ||
                      next.offsideExemption() ==
                          std::optional(BallTouch{.playerId = *played, .tick = context.tick()});
                  next.setOffside(passer && !exempt
                                      ? watchOf(current, *passer, context.tick(), rules.offside)
                                      : std::nullopt);
                  next.setOffsideExemption(std::nullopt);
                }
              },
          .intervalTicks = 1,
          .phaseTicks = 0};
}

}  // namespace ElyverseFootball::SimMatch
