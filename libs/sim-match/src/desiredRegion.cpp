#include "desiredRegion.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>

#include "responsibility.hpp"
#include "tactic.hpp"
#include "teamFrame.hpp"
#include "zones.hpp"

namespace ElyverseFootball::SimMatch {
namespace {

using SimCore::Vec2;
using SimTactics::Responsibility;
using SimTactics::TacticalPhase;

void validate(const PositioningConfig& config) {
  constexpr double kMax = std::numeric_limits<double>::max();
  const auto positive = [](const double value) { return value > 0.0 && value <= kMax; };
  const bool valid = config.intervalTicks >= 1 && positive(config.candidateSpacing) &&
                     positive(config.targetDistanceScale) && positive(config.spacingRadius) &&
                     positive(config.pressureRadius) && config.minConfidence >= 0.0 &&
                     config.minConfidence <= 1.0 && config.hysteresisCost >= 0.0 &&
                     config.hysteresisCost <= kMax && positive(config.maxShiftMeters);
  if (!valid) {
    throw std::invalid_argument("tactical movement: invalid configuration");
  }
}

// The goalkeeper's lateral range: he follows the ball across the goal, not
// out to the wings.
constexpr double kGoalkeeperFollow = 0.25;
constexpr double kGoalkeeperRange = 0.15;  // of the pitch width, each way

// Wing and halfspace centre lines, as fractions of the pitch width from the
// touchline on a slot's side of the pitch (docs/zones.md).
constexpr double kWingCentre = 0.1;
constexpr double kHalfspaceCentre = 0.3;

// The block follows the ball vertically half as much as it does across.
constexpr double kVerticalFollow = 0.5;

// The eight directions candidates lie in around the target.
constexpr double kDiagonal = 0.70710678118654752440;
constexpr std::array<Vec2, 8> kDirections{{{.x = 1.0, .y = 0.0},
                                           {.x = kDiagonal, .y = kDiagonal},
                                           {.x = 0.0, .y = 1.0},
                                           {.x = -kDiagonal, .y = kDiagonal},
                                           {.x = -1.0, .y = 0.0},
                                           {.x = -kDiagonal, .y = -kDiagonal},
                                           {.x = 0.0, .y = -1.0},
                                           {.x = kDiagonal, .y = -kDiagonal}}};

// The span of the outfield slots' base positions, which the phase's block
// is scaled to.
struct ShapeSpan {
  double minDepth = std::numeric_limits<double>::infinity();
  double maxDepth = -std::numeric_limits<double>::infinity();
  double minWidth = std::numeric_limits<double>::infinity();
  double maxWidth = -std::numeric_limits<double>::infinity();
};

[[nodiscard]] bool guardsGoal(const SimTactics::TacticSlot& slot) noexcept {
  return std::ranges::any_of(slot.responsibilities, [](const auto& entry) {
    return entry.responsibility == Responsibility::kGuardGoal;
  });
}

[[nodiscard]] ShapeSpan outfieldSpan(const SimTactics::Tactic& tactic) noexcept {
  ShapeSpan span;
  for (const SimTactics::TacticSlot& slot : tactic.slots()) {
    if (guardsGoal(slot)) {
      continue;
    }
    span.minDepth = std::min(span.minDepth, slot.position.depth);
    span.maxDepth = std::max(span.maxDepth, slot.position.depth);
    span.minWidth = std::min(span.minWidth, slot.position.width);
    span.maxWidth = std::max(span.maxWidth, slot.position.width);
  }
  return span;
}

// Where a value lies between min and max, in [0, 1]; 1/2 if they coincide.
[[nodiscard]] double fraction(const double value, const double min, const double max) noexcept {
  return max > min ? (value - min) / (max - min) : 0.5;
}

[[nodiscard]] double lerp(const double from, const double toward, const double amount) noexcept {
  return from + (amount * (toward - from));
}

// The tactic of the player's side; throws std::invalid_argument for a
// scripted side, which has none to position by.
[[nodiscard]] const SimTactics::Tactic& tacticOf(const MatchState& state, const TeamSide side) {
  const auto& tactic = state.tactics().of(side);
  if (!tactic) {
    throw std::invalid_argument(std::string("desired region: ") + std::string(teamSideName(side)) +
                                " plays no tactic");
  }
  return *tactic;
}

[[nodiscard]] Vec2 goalkeeperTarget(const MatchState& state, const TeamSide side,
                                    const SimTactics::TacticSlot& slot) noexcept {
  const Pitch& pitch = state.pitch();
  const double centre = pitch.widthMeters() / 2.0;
  const double range = kGoalkeeperRange * pitch.widthMeters();
  const double followed = centre + (kGoalkeeperFollow * (state.ball().position.y - centre));
  return pitch.clamp({.x = xAtDepth(side, slot.position.depth * pitch.lengthMeters(), pitch),
                      .y = std::clamp(followed, centre - range, centre + range)});
}

// A remembered player where the observer believes he is now.
struct Believed {
  Vec2 position;
  double confidence = 0.0;
  bool teammate = false;
};

}  // namespace

Vec2 tacticalTarget(const MatchState& state, const std::size_t playerIndex,
                    const TacticalPhase phase) {
  const PlayerMatchState& player = state.players()[playerIndex];
  const TeamSide side = player.side;
  const SimTactics::Tactic& tactic = tacticOf(state, side);
  const std::size_t slotNumber = slotIndex(state, playerIndex);
  const SimTactics::TacticSlot& slot = tactic.slots()[slotNumber];
  if (guardsGoal(slot)) {
    return goalkeeperTarget(state, side, slot);
  }

  const Pitch& pitch = state.pitch();
  const double length = pitch.lengthMeters();
  const double width = pitch.widthMeters();
  const SimTactics::PhaseInstruction& instruction = tactic.instruction(phase);
  const Vec2 ball = state.ball().position;
  const double ballDepth = depthOf(side, ball, pitch);

  // The block: its line where the phase puts it, shifted up or down with the
  // ball, never past either goal line.
  const ShapeSpan span = outfieldSpan(tactic);
  const double blockLength = instruction.blockLength * length;
  const double nominalCentre = (instruction.lineHeight * length) + (blockLength / 2.0);
  const double centre = lerp(nominalCentre, ballDepth, kVerticalFollow * instruction.ballShift);
  const double line = std::clamp(centre - (blockLength / 2.0), 0.0, length - blockLength);
  double depth = line + (fraction(slot.position.depth, span.minDepth, span.maxDepth) * blockLength);

  // Across: the block's width, centred between the pitch centre and the ball.
  const double centreY = lerp(width / 2.0, ball.y, instruction.ballShift);
  const double offset = fraction(slot.position.width, span.minWidth, span.maxWidth) - 0.5;
  double lateral = centreY + (offset * instruction.blockWidth * width);

  // Responsibilities pull toward their lane on the slot's side of the pitch
  // and, with the ball, behind it.
  const bool lowSide = slot.position.width <= 0.5;
  const auto laneY = [&](const double fromTouchline) {
    return lowSide ? fromTouchline * width : (1.0 - fromTouchline) * width;
  };
  lateral = lerp(lateral, laneY(kWingCentre),
                 tactic.responsibilityWeight(slotNumber, Responsibility::kProvideWidth));
  lateral = lerp(lateral, laneY(kHalfspaceCentre),
                 tactic.responsibilityWeight(slotNumber, Responsibility::kOccupyHalfspace));
  if (SimTactics::isInPossession(phase)) {
    const double far = std::max(0.0, ballDepth - kRestDefenceFar);
    const double near = std::max(0.0, ballDepth - kRestDefenceNear);
    depth = lerp(depth, std::clamp(depth, far, near),
                 tactic.responsibilityWeight(slotNumber, Responsibility::kHoldRestDefence));
  }
  return pitch.clamp({.x = xAtDepth(side, depth, pitch), .y = lateral});
}

PositionCost evaluatePosition(const MatchState& state, const std::size_t playerIndex,
                              const Vec2 candidate, const Vec2 target, const SimCore::SimTick now,
                              const double secondsPerTick, const PositioningConfig& config,
                              const PerceptionConfig& perception) {
  const PlayerMatchState& player = state.players()[playerIndex];
  const TeamSide side = player.side;
  const SimTactics::Tactic& tactic = tacticOf(state, side);
  PositionCost cost;
  cost.targetDistance =
      std::sqrt((candidate - target).lengthSquared()) / config.targetDistanceScale;

  for (const Observation& observation : state.perception(playerIndex).observations) {
    if (observation.entity.isBall() || observation.confidence < config.minConfidence) {
      continue;
    }
    const auto index = findPlayerIndex(state, observation.entity.playerId());
    if (!index) {
      continue;
    }
    const Vec2 believed = estimatePosition(observation, now, secondsPerTick, perception);
    const double distance = std::sqrt((believed - candidate).lengthSquared());
    if (state.players()[*index].side == side) {
      const double crowding = std::max(0.0, 1.0 - (distance / config.spacingRadius));
      cost.spacing += crowding * crowding;
    } else {
      cost.pressure +=
          observation.confidence * std::max(0.0, 1.0 - (distance / config.pressureRadius));
    }
  }

  const auto& grid = state.pitchControl();
  cost.occupancy = grid ? grid->controlAt(opponentOf(side), candidate) : 0.5;

  if (state.possession().team == side) {
    const Pitch& pitch = state.pitch();
    const double ballDepth = depthOf(side, state.ball().position, pitch);
    const double ahead = depthOf(side, candidate, pitch) - (ballDepth - kRestDefenceNear);
    const std::size_t slot = slotIndex(state, playerIndex);
    const double duty =
        std::min(1.0, 0.25 + tactic.responsibilityWeight(slot, Responsibility::kHoldRestDefence) +
                          tactic.responsibilityWeight(slot, Responsibility::kHoldDefensiveLine));
    cost.transitionRisk = std::clamp(ahead / (pitch.lengthMeters() / 3.0), 0.0, 1.0) * duty;
  }

  const SimTactics::PositioningWeights& weights = tactic.principles().positioning;
  cost.total = (weights.targetDistance * cost.targetDistance) + (weights.spacing * cost.spacing) +
               (weights.pressure * cost.pressure) + (weights.occupancy * cost.occupancy) +
               (weights.transitionRisk * cost.transitionRisk);
  return cost;
}

DesiredRegion chooseDesiredRegion(const MatchState& state, const std::size_t playerIndex,
                                  const TacticalPhase phase, const SimCore::SimTick now,
                                  const double secondsPerTick, const PositioningConfig& config,
                                  const PerceptionConfig& perception) {
  const Pitch& pitch = state.pitch();
  const Vec2 target = tacticalTarget(state, playerIndex, phase);
  const auto costOf = [&](const Vec2 candidate) {
    return evaluatePosition(state, playerIndex, candidate, target, now, secondsPerTick, config,
                            perception)
        .total;
  };

  Vec2 best = target;
  double bestCost = costOf(target);
  for (const double radius : {config.candidateSpacing, 2.0 * config.candidateSpacing}) {
    for (const Vec2 direction : kDirections) {
      const Vec2 candidate = pitch.clamp(target + (direction * radius));
      const double cost = costOf(candidate);
      if (cost < bestCost) {
        best = candidate;
        bestCost = cost;
      }
    }
  }

  // Hysteresis: the current centre wins unless a candidate is clearly
  // better. Smoothing: the centre never jumps.
  const auto& previous = state.tactical(playerIndex).region;
  Vec2 center = best;
  if (previous) {
    if (costOf(previous->center) - config.hysteresisCost <= bestCost) {
      center = previous->center;
    }
    const Vec2 shift = center - previous->center;
    const double distance = std::sqrt(shift.lengthSquared());
    if (distance > config.maxShiftMeters) {
      center = previous->center + (shift * (config.maxShiftMeters / distance));
    }
  }
  return {.tacticalTarget = target,
          .center = center,
          .cost = evaluatePosition(state, playerIndex, center, target, now, secondsPerTick, config,
                                   perception)};
}

MatchSystem makeTacticalMovementSystem(const PositioningConfig& config,
                                       const PerceptionConfig& perception) {
  validate(config);
  return {.name = std::string(kTacticalMovementSystemName),
          .update =
              [config, perception](const MatchStepContext& context, const MatchState& current,
                                   MatchStateWriter& next) {
                for (std::size_t index = 0; index < current.players().size(); ++index) {
                  const PlayerMatchState& player = current.players()[index];
                  const auto& phase = current.phase(player.side);
                  const bool onTheBall = current.ball().owner == player.playerId;
                  const bool chasing = current.chaser(player.side) == player.playerId;
                  if (!phase || onTheBall || chasing) {
                    continue;
                  }
                  const DesiredRegion region =
                      chooseDesiredRegion(current, index, phase->phase, context.tick(),
                                          context.secondsPerTick(), config, perception);
                  next.tactical(index).region = region;
                  next.setPlayerTarget(index, region.center);
                }
              },
          .intervalTicks = config.intervalTicks,
          .phaseTicks = 0};
}

}  // namespace ElyverseFootball::SimMatch
