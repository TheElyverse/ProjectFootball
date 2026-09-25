#include "matchStateHash.hpp"

#include <cstddef>
#include <optional>

#include "stableHash.hpp"
#include "tacticHash.hpp"
#include "vec2.hpp"

namespace ElyverseFootball::SimMatch {
namespace {

using SimCore::StableHasher;
using SimCore::Vec2;

void addVec2(StableHasher& hasher, const Vec2 vector) noexcept {
  hasher.addDouble(vector.x);
  hasher.addDouble(vector.y);
}

// A presence flag first, so an empty optional and one holding zeros differ.
void addOptionalVec2(StableHasher& hasher, const std::optional<Vec2>& vector) noexcept {
  hasher.addBool(vector.has_value());
  if (vector) {
    addVec2(hasher, *vector);
  }
}

void addPlayer(StableHasher& hasher, const PlayerMatchState& player) noexcept {
  hasher.addU64(player.playerId.value());
  hasher.addU64(static_cast<std::uint64_t>(player.side));
  addVec2(hasher, player.position);
  addVec2(hasher, player.velocity);
  hasher.addDouble(player.attributes.maxSpeed);
  hasher.addDouble(player.attributes.acceleration);
  addOptionalVec2(hasher, player.target);
  addVec2(hasher, player.facing);
}

void addObservation(StableHasher& hasher, const Observation& observation) noexcept {
  hasher.addBool(observation.entity.isBall());
  hasher.addU64(observation.entity.playerId().value());
  addVec2(hasher, observation.position);
  addVec2(hasher, observation.velocity);
  hasher.addDouble(observation.confidence);
  hasher.addI64(observation.lastSeen.value());
}

void addPitchControl(StableHasher& hasher, const std::optional<PitchControlGrid>& grid) {
  hasher.addBool(grid.has_value());
  if (!grid) {
    return;
  }
  hasher.addU64(grid->columns());
  hasher.addU64(grid->rows());
  hasher.addDouble(grid->cellSize());
  hasher.addDouble(grid->controlSeconds());
  for (std::size_t column = 0; column < grid->columns(); ++column) {
    for (std::size_t row = 0; row < grid->rows(); ++row) {
      const GridCell cell{.column = column, .row = row};
      hasher.addDouble(grid->arrivalSeconds(TeamSide::kHome, cell));
      hasher.addDouble(grid->arrivalSeconds(TeamSide::kAway, cell));
    }
  }
}

void addCost(StableHasher& hasher, const PositionCost& cost) noexcept {
  hasher.addDouble(cost.targetDistance);
  hasher.addDouble(cost.spacing);
  hasher.addDouble(cost.pressure);
  hasher.addDouble(cost.occupancy);
  hasher.addDouble(cost.transitionRisk);
  hasher.addDouble(cost.total);
}

void addTactical(StableHasher& hasher, const PlayerTacticalState& tactical) noexcept {
  hasher.addBool(tactical.region.has_value());
  if (tactical.region) {
    addVec2(hasher, tactical.region->tacticalTarget);
    addVec2(hasher, tactical.region->center);
    addCost(hasher, tactical.region->cost);
  }
  hasher.addBool(tactical.action.has_value());
  if (tactical.action) {
    hasher.addU64(static_cast<std::uint64_t>(tactical.action->type));
    addVec2(hasher, tactical.action->target);
    hasher.addBool(tactical.action->subject.has_value());
    hasher.addU64(tactical.action->subject.value_or(SimCore::PlayerId::invalid()).value());
    hasher.addI64(tactical.action->decidedAt.value());
    hasher.addBool(tactical.action->withBall);
  }
  hasher.addBool(tactical.lastChallenge.has_value());
  hasher.addI64(tactical.lastChallenge.value_or(SimCore::SimTick(0)).value());
}

}  // namespace

std::uint64_t hashMatchState(const MatchState& state) noexcept {
  StableHasher hasher;
  hasher.addDouble(state.pitch().lengthMeters());
  hasher.addDouble(state.pitch().widthMeters());
  hasher.addI64(state.playersPerSide());
  hasher.addU64(state.players().size());
  for (const PlayerMatchState& player : state.players()) {
    addPlayer(hasher, player);
  }
  addVec2(hasher, state.ball().position);
  addVec2(hasher, state.ball().velocity);
  hasher.addBool(state.ball().owner.has_value());
  hasher.addU64(state.ball().owner.value_or(SimCore::PlayerId::invalid()).value());
  const auto& touch = state.ball().lastTouch;
  hasher.addBool(touch.has_value());
  if (touch) {
    hasher.addU64(touch->playerId.value());
    hasher.addI64(touch->tick.value());
  }
  for (std::size_t index = 0; index < state.players().size(); ++index) {
    const PlayerPerception& perception = state.perception(index);
    hasher.addU64(perception.observations.size());
    for (const Observation& observation : perception.observations) {
      addObservation(hasher, observation);
    }
  }
  const auto& pass = state.pendingPass();
  hasher.addBool(pass.has_value());
  if (pass) {
    hasher.addU64(pass->passer.value());
    addVec2(hasher, pass->target);
    hasher.addDouble(pass->speed);
    hasher.addBool(pass->receiver.has_value());
    hasher.addU64(pass->receiver.value_or(SimCore::PlayerId::invalid()).value());
  }
  for (const TeamSide side : {TeamSide::kHome, TeamSide::kAway}) {
    const auto& tactic = state.tactics().of(side);
    hasher.addBool(tactic.has_value());
    if (tactic) {
      hasher.addU64(SimTactics::contentHash(*tactic));
    }
  }
  const TeamPossession& possession = state.possession();
  hasher.addBool(possession.team.has_value());
  hasher.addU64(static_cast<std::uint64_t>(possession.team.value_or(TeamSide::kHome)));
  hasher.addI64(possession.since.value());
  hasher.addBool(possession.fromOpponent);
  for (const TeamSide side : {TeamSide::kHome, TeamSide::kAway}) {
    const auto& phase = state.phase(side);
    hasher.addBool(phase.has_value());
    if (phase) {
      hasher.addU64(static_cast<std::uint64_t>(phase->phase));
      hasher.addI64(phase->since.value());
    }
  }
  addPitchControl(hasher, state.pitchControl());
  for (const TeamSide side : {TeamSide::kHome, TeamSide::kAway}) {
    const auto& chaser = state.chaser(side);
    hasher.addBool(chaser.has_value());
    hasher.addU64(chaser.value_or(SimCore::PlayerId::invalid()).value());
  }
  for (std::size_t index = 0; index < state.players().size(); ++index) {
    addTactical(hasher, state.tactical(index));
  }
  return hasher.value();
}

}  // namespace ElyverseFootball::SimMatch
