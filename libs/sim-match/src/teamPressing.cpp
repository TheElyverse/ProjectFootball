#include "teamPressing.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <string>
#include <tuple>

#include "actionGeometry.hpp"
#include "matchEvents.hpp"
#include "passCandidates.hpp"
#include "pressingActions.hpp"
#include "tactic.hpp"
#include "teamFrame.hpp"
#include "zones.hpp"

namespace ElyverseFootball::SimMatch {
namespace {

using ActionGeometry::distanceBetween;
using SimCore::PlayerId;
using SimCore::SimTick;
using SimCore::Vec2;
using SimTactics::PressingTrigger;

// An opponent this close to his own goal line is his goalkeeper, no option
// to press or block.
constexpr double kKeeperDepth = 8.0;  // m

[[nodiscard]] double secondsSince(const SimTick earlier, const SimTick now,
                                  const double secondsPerTick) noexcept {
  return static_cast<double>(now.value() - earlier.value()) * secondsPerTick;
}

// What a defender sees right now: what he saw in his latest perception
// update, no longer ago than one perception interval.
[[nodiscard]] bool sees(const MatchState& state, const std::size_t observer,
                        const ObservedEntity entity, const SimTick now,
                        const PerceptionConfig& perception) {
  const Observation* observation = state.perception(observer).find(entity);
  return observation != nullptr &&
         now.value() - observation->lastSeen.value() <= perception.intervalTicks;
}

// The facts about the carrier a defender who sees him can read off the
// match: his last reception, the pass that led to it, his facing and his
// teammates nearby.
class TriggerCheck {
 public:
  TriggerCheck(const MatchState& state, const std::size_t carrierIndex, const SimTick now,
               const double secondsPerTick, const PressingConfig& config)
      : state_(&state),
        carrier_(&state.players()[carrierIndex]),
        now_(now),
        secondsPerTick_(secondsPerTick),
        config_(&config) {}

  [[nodiscard]] bool recentReception() const {
    const auto& reception = state_->lastReception();
    return reception && reception->player == carrier_->playerId &&
           secondsSince(reception->tick, now_, secondsPerTick_) <= config_->recentSeconds;
  }

  [[nodiscard]] bool poorFirstTouch() const {
    return recentReception() && state_->lastReception().value_or(ReceptionRecord{}).ballSpeed >=
                                    config_->heavyTouchSpeed;
  }

  [[nodiscard]] bool backPass() const {
    const auto& pass = state_->lastPass();
    if (!recentReception() || !pass) {
      return false;
    }
    const auto passer = findPlayerIndex(*state_, pass->passer);
    if (!passer || state_->players()[*passer].side != carrier_->side) {
      return false;
    }
    const Pitch& pitch = state_->pitch();
    return depthOf(carrier_->side, pass->from, pitch) -
               depthOf(carrier_->side, carrier_->position, pitch) >=
           config_->backPassMeters;
  }

  [[nodiscard]] bool facingOwnGoal() const {
    return recentReception() &&
           carrier_->facing.x * attackingDirection(carrier_->side) < config_->facingOwnGoal;
  }

  // No teammate of the carrier near him, as the defender remembers them.
  [[nodiscard]] bool isolated(const std::size_t defender,
                              const PerceptionConfig& perception) const {
    if (!recentReception()) {
      return false;
    }
    return std::ranges::none_of(
        rememberedPlayers(*state_, defender, now_, secondsPerTick_, perception, 0.3),
        [this](const RememberedPlayer& other) {
          return !other.teammate && other.playerId != carrier_->playerId &&
                 distanceBetween(other.position, carrier_->position) <= config_->isolationRadius;
        });
  }

 private:
  const MatchState* state_;
  const PlayerMatchState* carrier_;
  SimTick now_;
  double secondsPerTick_;
  const PressingConfig* config_;
};

// A slow pass toward a receiver of the other side that the defender sees
// rolling; the receiver is whom to press.
[[nodiscard]] std::optional<PlayerId> slowPassReceiver(
    const MatchState& state, const std::size_t defender, const TeamSide pressingSide,
    const SimTick now, const double secondsPerTick, const PressingConfig& config,
    const PerceptionConfig& perception) {
  const BallState& ball = state.ball();
  const auto& pass = state.lastPass();
  if (ball.owner || !pass || !pass->receiver ||
      secondsSince(pass->tick, now, secondsPerTick) > config.recentSeconds ||
      !sees(state, defender, ObservedEntity::ball(), now, perception)) {
    return std::nullopt;
  }
  const auto receiver = findPlayerIndex(state, *pass->receiver);
  const double speed = std::sqrt(ball.velocity.lengthSquared());
  const bool slow = speed > 0.0 && speed < config.slowPassSpeed;
  if (!receiver || state.players()[*receiver].side == pressingSide || !slow) {
    return std::nullopt;
  }
  return pass->receiver;
}

[[nodiscard]] bool isOpponentKeeper(const MatchState& state, const PlayerMatchState& player) {
  return depthOf(player.side, player.position, state.pitch()) < kKeeperDepth;
}

// Everything spotting a trigger needs besides the defender and the trigger.
struct TriggerScene {
  const MatchState* state = nullptr;
  TeamSide pressingSide = TeamSide::kHome;
  SimTick now;
  double secondsPerTick = 0.0;
  const PressingConfig* config = nullptr;
  const PerceptionConfig* perception = nullptr;

  // The carrier the defender at this index spots the trigger on, if he does.
  [[nodiscard]] std::optional<PlayerId> spots(const std::size_t defender,
                                              const PressingTrigger trigger) const {
    if (trigger == PressingTrigger::kSlowPass) {
      return slowPassReceiver(*state, defender, pressingSide, now, secondsPerTick, *config,
                              *perception);
    }
    const auto& owner = state->ball().owner;
    const auto carrierIndex = owner ? findPlayerIndex(*state, *owner) : std::nullopt;
    if (!carrierIndex) {
      return std::nullopt;
    }
    const PlayerMatchState& carrier = state->players()[*carrierIndex];
    if (carrier.side == pressingSide ||
        !sees(*state, defender, ObservedEntity::player(carrier.playerId), now, *perception)) {
      return std::nullopt;
    }
    const TriggerCheck check(*state, *carrierIndex, now, secondsPerTick, *config);
    bool spotted = false;
    switch (trigger) {
      case PressingTrigger::kPoorFirstTouch:
        spotted = check.poorFirstTouch();
        break;
      case PressingTrigger::kBackPass:
        spotted = check.backPass();
        break;
      case PressingTrigger::kReceiverFacingOwnGoal:
        spotted = check.facingOwnGoal();
        break;
      case PressingTrigger::kIsolatedReceiver:
        spotted = check.isolated(defender, *perception);
        break;
      case PressingTrigger::kSlowPass:
        break;
    }
    return spotted ? std::optional(carrier.playerId) : std::nullopt;
  }
};

}  // namespace

void validate(const PressingConfig& config) {
  constexpr double kMax = std::numeric_limits<double>::max();
  const auto positive = [](const double value) { return value > 0.0 && value <= kMax; };
  const bool valid = config.intervalTicks >= 1 && positive(config.triggerRadius) &&
                     positive(config.recentSeconds) && positive(config.heavyTouchSpeed) &&
                     positive(config.slowPassSpeed) && positive(config.isolationRadius) &&
                     config.facingOwnGoal >= -1.0 && config.facingOwnGoal <= 1.0 &&
                     positive(config.backPassMeters) && config.maxJoiners >= 1 &&
                     config.phaseIntensity >= 0.0 && config.phaseIntensity <= 1.0 &&
                     positive(config.maxPressSeconds);
  if (!valid) {
    throw std::invalid_argument("team pressing: invalid configuration");
  }
}

std::optional<SpottedTrigger> spotTrigger(const MatchState& state, const TeamSide pressingSide,
                                          const SimTick now, const double secondsPerTick,
                                          const PressingConfig& config,
                                          const PerceptionConfig& perception) {
  const auto& tactic = state.tactics().of(pressingSide);
  if (!tactic) {
    return std::nullopt;
  }
  const TriggerScene scene{.state = &state,
                           .pressingSide = pressingSide,
                           .now = now,
                           .secondsPerTick = secondsPerTick,
                           .config = &config,
                           .perception = &perception};
  for (const PressingTrigger trigger : tactic->principles().pressingTriggers) {
    for (std::size_t index = 0; index < state.players().size(); ++index) {
      const PlayerMatchState& defender = state.players()[index];
      const bool looking =
          defender.side == pressingSide && !isGoalkeeper(state, index) &&
          distanceBetween(defender.position, state.ball().position) <= config.triggerRadius;
      if (!looking) {
        continue;
      }
      if (const auto carrier = scene.spots(index, trigger)) {
        return SpottedTrigger{.trigger = trigger, .carrier = *carrier};
      }
    }
  }
  return std::nullopt;
}

int pressJoiners(const double intensity, const PressingConfig& config) noexcept {
  return static_cast<int>(std::round(intensity * static_cast<double>(config.maxJoiners)));
}

std::vector<PressAssignment> assignPressRoles(const MatchState& state, const TeamSide pressingSide,
                                              const PressRequest& request) {
  const auto carrierIndex = findPlayerIndex(state, request.carrier);
  if (!carrierIndex || request.joiners < 1) {
    return {};
  }
  const Vec2 carrierAt = state.players()[*carrierIndex].position;
  const Pitch& pitch = state.pitch();
  const Vec2 ownGoal{.x = xAtDepth(pressingSide, 0.0, pitch), .y = pitch.widthMeters() / 2.0};

  // Free outfield players of the pressing side, and the carrier's options,
  // nearest to the carrier first.
  std::vector<std::size_t> free;
  std::vector<std::size_t> options;
  for (std::size_t index = 0; index < state.players().size(); ++index) {
    const PlayerMatchState& player = state.players()[index];
    if (player.side == pressingSide) {
      if (!isGoalkeeper(state, index)) {
        free.push_back(index);
      }
    } else if (index != *carrierIndex && !isOpponentKeeper(state, player)) {
      options.push_back(index);
    }
  }
  const auto nearerTo = [&state](const Vec2 point) {
    return [&state, point](const std::size_t first, const std::size_t second) {
      const PlayerMatchState& one = state.players()[first];
      const PlayerMatchState& other = state.players()[second];
      return std::tuple(distanceBetween(one.position, point), one.playerId) <
             std::tuple(distanceBetween(other.position, point), other.playerId);
    };
  };
  std::ranges::sort(free, nearerTo(carrierAt));
  std::ranges::sort(options, nearerTo(carrierAt));
  if (free.empty()) {
    return {};
  }

  std::vector<PressAssignment> assignments;
  const std::size_t presser = free.front();
  free.erase(free.begin());
  assignments.push_back({.player = state.players()[presser].playerId,
                         .role = PressRole::kPress,
                         .subject = request.carrier});

  // Takes the free player nearest to where a role would put him.
  const auto take = [&](const auto spotOf) -> std::optional<std::size_t> {
    if (free.empty()) {
      return std::nullopt;
    }
    const auto costOf = [&](const std::size_t index) {
      const PlayerMatchState& player = state.players()[index];
      return std::tuple(distanceBetween(player.position, spotOf(player.position)), player.playerId);
    };
    const auto best =
        std::ranges::min_element(free, [&](const std::size_t first, const std::size_t second) {
          return costOf(first) < costOf(second);
        });
    const std::size_t chosen = *best;
    free.erase(best);
    return chosen;
  };

  const bool cover = request.joiners >= 3;
  const int blockers = request.joiners - 1 - (cover ? 1 : 0);
  for (int block = 0; block < blockers && std::cmp_less(block, options.size()); ++block) {
    const PlayerMatchState& option = state.players()[options.at(static_cast<std::size_t>(block))];
    const auto spot = [&](const Vec2 from) {
      return laneBlockTarget({.carrier = carrierAt, .receiver = option.position}, from, 2.0);
    };
    if (const auto blocker = take(spot)) {
      assignments.push_back({.player = state.players()[*blocker].playerId,
                             .role = PressRole::kBlockLane,
                             .subject = option.playerId});
    }
  }
  if (cover) {
    const Vec2 behind =
        coverTarget(state.players()[presser].position, ownGoal, request.coverDistance);
    if (const auto coverer = take([behind](const Vec2 /*from*/) { return behind; })) {
      assignments.push_back({.player = state.players()[*coverer].playerId,
                             .role = PressRole::kCover,
                             .subject = state.players()[presser].playerId});
    }
  }
  return assignments;
}

namespace {

// How a side's press in progress ends at tick now, if it does.
[[nodiscard]] std::optional<PressOutcome> pressOutcome(const MatchState& current,
                                                       const TeamSide side, const TeamPress& press,
                                                       const SimTick now,
                                                       const double secondsPerTick,
                                                       const PressingConfig& config) {
  const auto owner = current.ball().owner;
  const auto ownerIndex = owner ? findPlayerIndex(current, *owner) : std::nullopt;
  if (ownerIndex && current.players()[*ownerIndex].side == side) {
    return PressOutcome::kBallRegained;
  }
  if (owner && *owner != press.carrier) {
    return PressOutcome::kPassedOut;
  }
  if (secondsSince(press.since, now, secondsPerTick) >= config.maxPressSeconds) {
    return PressOutcome::kCarrierEscaped;
  }
  return std::nullopt;
}

// Whom a side without a press should press now, and on which trigger --
// empty trigger for a press of the pressing phase.
struct PressStart {
  PlayerId carrier;
  std::optional<PressingTrigger> trigger;
};

[[nodiscard]] std::optional<PressStart> pressStart(const MatchStepContext& context,
                                                   const MatchState& current, const TeamSide side,
                                                   const double intensity,
                                                   const PressingConfig& config,
                                                   const PerceptionConfig& perception) {
  if (const auto spotted = spotTrigger(current, side, context.tick(), context.secondsPerTick(),
                                       config, perception)) {
    return PressStart{.carrier = spotted->carrier, .trigger = spotted->trigger};
  }
  const auto& phase = current.phase(side);
  const auto owner = current.ball().owner;
  const auto ownerIndex = owner ? findPlayerIndex(current, *owner) : std::nullopt;
  const bool phasePress = phase && phase->phase == SimTactics::TacticalPhase::kPressing &&
                          intensity >= config.phaseIntensity && ownerIndex &&
                          current.players()[*ownerIndex].side != side;
  if (phasePress) {
    return PressStart{.carrier = *owner, .trigger = std::nullopt};
  }
  return std::nullopt;
}

}  // namespace

MatchSystem makePressingSystem(const PressingConfig& config, const PerceptionConfig& perception,
                               const double coverDistance) {
  validate(config);
  return {
      .name = std::string(kPressingSystemName),
      .update =
          [config, perception, coverDistance](const MatchStepContext& context,
                                              const MatchState& current, MatchStateWriter& next) {
            for (const TeamSide side : {TeamSide::kHome, TeamSide::kAway}) {
              const auto& tactic = current.tactics().of(side);
              const auto& phase = current.phase(side);
              if (!tactic || !phase) {
                continue;
              }
              if (const auto& press = current.press(side)) {
                if (const auto outcome = pressOutcome(current, side, *press, context.tick(),
                                                      context.secondsPerTick(), config)) {
                  next.setPress(side, std::nullopt);
                  context.record(
                      PressingEnded{.tick = context.tick(), .side = side, .outcome = *outcome});
                }
                continue;
              }
              const double intensity = tactic->instruction(phase->phase).pressingIntensity;
              const int joiners = pressJoiners(intensity, config);
              if (current.possession().team == side || joiners < 1) {
                continue;
              }
              const auto start = pressStart(context, current, side, intensity, config, perception);
              if (!start) {
                continue;
              }
              std::vector<PressAssignment> assignments = assignPressRoles(
                  current, side,
                  {.carrier = start->carrier, .joiners = joiners, .coverDistance = coverDistance});
              if (assignments.empty()) {
                continue;
              }
              next.setPress(side, TeamPress{.carrier = start->carrier,
                                            .since = context.tick(),
                                            .trigger = start->trigger,
                                            .assignments = assignments});
              context.record(PressingStarted{.tick = context.tick(),
                                             .side = side,
                                             .carrier = start->carrier,
                                             .trigger = start->trigger,
                                             .assignments = std::move(assignments)});
            }
          },
      .intervalTicks = config.intervalTicks,
      .phaseTicks = 0};
}

}  // namespace ElyverseFootball::SimMatch
