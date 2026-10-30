#include "matchStateHash.hpp"

#include <cstddef>
#include <optional>
#include <variant>

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

void addAttributes(StableHasher& hasher, const PlayerAttributes& attributes) noexcept {
  hasher.addDouble(attributes.maxSpeed);
  hasher.addDouble(attributes.acceleration);
  hasher.addDouble(attributes.shotAccuracy);
  hasher.addDouble(attributes.shotTechnique);
  hasher.addU64(static_cast<std::uint64_t>(attributes.strongFoot));
  hasher.addDouble(attributes.weakFootAccuracy);
  hasher.addDouble(attributes.keeperPositioning);
  hasher.addDouble(attributes.keeperAnticipation);
  hasher.addDouble(attributes.keeperReflexes);
  hasher.addDouble(attributes.keeperHandling);
  hasher.addDouble(attributes.jumping);
  hasher.addDouble(attributes.heading);
  hasher.addDouble(attributes.strength);
  hasher.addDouble(attributes.ballControl);
  hasher.addDouble(attributes.dribbling);
  hasher.addDouble(attributes.decisionMaking);
}

void addPlayer(StableHasher& hasher, const PlayerMatchState& player) noexcept {
  hasher.addU64(player.playerId.value());
  hasher.addU64(static_cast<std::uint64_t>(player.side));
  addVec2(hasher, player.position);
  addVec2(hasher, player.velocity);
  addAttributes(hasher, player.attributes);
  addOptionalVec2(hasher, player.target);
  addVec2(hasher, player.facing);
  hasher.addDouble(player.fitness.sprintReserve);
  hasher.addI64(player.fitness.playedTicks);
  hasher.addDouble(player.fitness.topSpeedShare);
  hasher.addI64(player.yellowCards);
  hasher.addBool(player.sentOff);
}

void addShot(StableHasher& hasher, const ShotRecord& shot) noexcept {
  hasher.addU64(shot.shooter.value());
  addVec2(hasher, shot.from);
  hasher.addI64(shot.tick.value());
  hasher.addBool(shot.deflection.has_value());
  if (shot.deflection) {
    hasher.addU64(shot.deflection->playerId.value());
    hasher.addI64(shot.deflection->tick.value());
  }
  hasher.addBool(shot.resolved);
  hasher.addBool(shot.parry.has_value());
  if (shot.parry) {
    hasher.addU64(shot.parry->playerId.value());
    hasher.addI64(shot.parry->tick.value());
  }
  hasher.addBool(shot.saved);
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
  hasher.addBool(tactical.duel.has_value());
  if (const auto& duel = tactical.duel) {
    hasher.addU64(duel->carrier.value());
    hasher.addI64(duel->since.value());
    hasher.addU64(static_cast<std::uint64_t>(duel->carrierMove));
    hasher.addU64(static_cast<std::uint64_t>(duel->defenderMove));
    hasher.addDouble(duel->carrierBalance);
    hasher.addDouble(duel->defenderBalance);
  }
  hasher.addBool(tactical.sweep.has_value());
  if (tactical.sweep) {
    hasher.addU64(tactical.sweep->touchedBy.value());
    hasher.addI64(tactical.sweep->touchedAt.value());
    hasher.addDouble(tactical.sweep->misjudgement);
    hasher.addBool(tactical.sweep->coming);
  }
  hasher.addBool(tactical.dive.has_value());
  if (const auto& dive = tactical.dive) {
    hasher.addU64(dive->touchedBy.value());
    hasher.addI64(dive->touchedAt.value());
    hasher.addI64(dive->tick.value());
    addVec2(hasher, dive->origin);
    addVec2(hasher, dive->normal);
    hasher.addDouble(dive->feet);
    hasher.addDouble(dive->target.across);
    hasher.addDouble(dive->target.up);
    hasher.addDouble(dive->runSeconds);
    hasher.addDouble(dive->landSeconds);
    hasher.addDouble(dive->recoverySeconds);
  }
  hasher.addBool(tactical.lastJump.has_value());
  hasher.addI64(tactical.lastJump.value_or(SimCore::SimTick(0)).value());
  hasher.addBool(tactical.handsReleased.has_value());
  hasher.addI64(tactical.handsReleased.value_or(SimCore::SimTick(0)).value());
  hasher.addBool(tactical.scan.has_value());
  if (const auto& scan = tactical.scan) {
    hasher.addI64(scan->since.value());
    hasher.addI64(scan->looks);
    addVec2(hasher, scan->gaze);
  }
  hasher.addBool(tactical.carry.has_value());
  if (const auto& carry = tactical.carry) {
    hasher.addU64(static_cast<std::uint64_t>(carry->kind));
    addVec2(hasher, carry->target);
    hasher.addBool(carry->subject.has_value());
    hasher.addU64(carry->subject.value_or(SimCore::PlayerId::invalid()).value());
    hasher.addI64(carry->since.value());
    addVec2(hasher, carry->focus);
    addVec2(hasher, carry->gaze);
    hasher.addBool(carry->touchedAt.has_value());
    hasher.addI64(carry->touchedAt.value_or(SimCore::SimTick(0)).value());
    hasher.addBool(carry->retakenAt.has_value());
    hasher.addI64(carry->retakenAt.value_or(SimCore::SimTick(0)).value());
    hasher.addBool(carry->dribbling);
  }
  hasher.addBool(tactical.stumbledUntil.has_value());
  hasher.addI64(tactical.stumbledUntil.value_or(SimCore::SimTick(0)).value());
}

void addPress(StableHasher& hasher, const std::optional<TeamPress>& press) noexcept {
  hasher.addBool(press.has_value());
  if (!press) {
    return;
  }
  hasher.addU64(press->carrier.value());
  hasher.addI64(press->since.value());
  hasher.addBool(press->trigger.has_value());
  hasher.addU64(static_cast<std::uint64_t>(
      press->trigger.value_or(SimTactics::PressingTrigger::kPoorFirstTouch)));
  hasher.addU64(press->assignments.size());
  for (const PressAssignment& assignment : press->assignments) {
    hasher.addU64(assignment.player.value());
    hasher.addU64(static_cast<std::uint64_t>(assignment.role));
    hasher.addU64(assignment.subject.value());
  }
}

void addPendingAction(StableHasher& hasher, const PendingAction& action) noexcept {
  hasher.addU64(action.index());
  if (const auto* pass = std::get_if<PassIntent>(&action)) {
    hasher.addU64(pass->passer.value());
    addVec2(hasher, pass->target);
    hasher.addDouble(pass->speed);
    hasher.addBool(pass->receiver.has_value());
    hasher.addU64(pass->receiver.value_or(SimCore::PlayerId::invalid()).value());
    hasher.addBool(pass->lofted);
    hasher.addBool(pass->cross.has_value());
    hasher.addByte(static_cast<std::uint8_t>(pass->cross.value_or(CrossArea::kRun)));
    hasher.addDouble(pass->height);
    hasher.addDouble(pass->curvature);
  }
  if (const auto* shot = std::get_if<ShotIntent>(&action)) {
    hasher.addU64(shot->shooter.value());
    addVec2(hasher, shot->target);
    hasher.addDouble(shot->height);
    hasher.addDouble(shot->speed);
    hasher.addByte(static_cast<std::uint8_t>(shot->type));
    hasher.addDouble(shot->curvature);
    hasher.addBool(shot->placed);
  }
  if (const auto* carry = std::get_if<CarryIntent>(&action)) {
    hasher.addU64(carry->carrier.value());
    addVec2(hasher, carry->target);
    hasher.addDouble(carry->speed);
    hasher.addDouble(carry->seconds);
    hasher.addDouble(carry->errorFactor);
  }
}

}  // namespace

namespace {

// Substitutions ordered or made (docs/match-engine.md#substitutions).
void addSubstitutions(StableHasher& hasher, const std::span<const Substitution> substitutions) {
  hasher.addU64(substitutions.size());
  for (const Substitution& substitution : substitutions) {
    hasher.addU64(static_cast<std::uint64_t>(substitution.side));
    hasher.addU64(substitution.off.value());
    hasher.addU64(substitution.on.value());
    addAttributes(hasher, substitution.attributes);
    hasher.addI64(substitution.ordered.value());
  }
}

// The tactics on their way to the players (docs/match-engine.md#instructions).
void addPendingTactics(StableHasher& hasher, const MatchState& state) {
  for (const TeamSide side : {TeamSide::kHome, TeamSide::kAway}) {
    const auto& pending = state.pendingTactic(side);
    hasher.addBool(pending.has_value());
    if (pending) {
      hasher.addU64(SimTactics::contentHash(pending->tactic));
      hasher.addI64(pending->given.value());
    }
  }
}

// The touch of an indirect free kick nobody else has touched yet, if any.
void addIndirectKick(StableHasher& hasher, const std::optional<BallTouch>& kick) noexcept {
  hasher.addBool(kick.has_value());
  if (kick) {
    hasher.addU64(kick->playerId.value());
    hasher.addI64(kick->tick.value());
  }
}

// A set piece being set up, if any.
void addSetPiece(StableHasher& hasher, const std::optional<SetPiece>& setPiece) noexcept {
  hasher.addBool(setPiece.has_value());
  if (setPiece) {
    hasher.addByte(static_cast<std::uint8_t>(setPiece->kind));
    hasher.addU64(static_cast<std::uint64_t>(setPiece->side));
    hasher.addU64(setPiece->taker.value());
    addVec2(hasher, setPiece->spot);
    hasher.addI64(setPiece->awarded.value());
    hasher.addBool(setPiece->indirect);
  }
}

// The play the offside rule watches and the touch free of it.
void addOffside(StableHasher& hasher, const MatchState& state) noexcept {
  const auto& offside = state.offside();
  hasher.addBool(offside.has_value());
  if (offside) {
    hasher.addU64(static_cast<std::uint64_t>(offside->side));
    hasher.addU64(offside->passer.value());
    hasher.addI64(offside->tick.value());
    hasher.addDouble(offside->line);
    hasher.addU64(offside->offsidePlayers);
  }
  const auto& exemption = state.offsideExemption();
  hasher.addBool(exemption.has_value());
  if (exemption) {
    hasher.addU64(exemption->playerId.value());
    hasher.addI64(exemption->tick.value());
  }
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
  hasher.addDouble(state.ball().height);
  hasher.addDouble(state.ball().verticalVelocity);
  hasher.addDouble(state.ball().spin);
  hasher.addDouble(state.ball().sideSpin);
  hasher.addBool(state.ball().owner.has_value());
  hasher.addU64(state.ball().owner.value_or(SimCore::PlayerId::invalid()).value());
  hasher.addBool(state.ball().held);
  hasher.addBool(state.ball().touch.has_value());
  if (const auto& carried = state.ball().touch) {
    hasher.addI64(carried->tick.value());
    hasher.addDouble(carried->seconds);
  }
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
  for (std::size_t index = 0; index < state.players().size(); ++index) {
    const std::optional<PendingAction>& action = state.pendingAction(index);
    hasher.addBool(action.has_value());
    if (action) {
      addPendingAction(hasher, *action);
    }
  }
  const auto& lastPass = state.lastPass();
  hasher.addBool(lastPass.has_value());
  if (lastPass) {
    hasher.addU64(lastPass->passer.value());
    addVec2(hasher, lastPass->from);
    hasher.addI64(lastPass->tick.value());
    hasher.addBool(lastPass->receiver.has_value());
    hasher.addU64(lastPass->receiver.value_or(SimCore::PlayerId::invalid()).value());
  }
  const auto& lastShot = state.lastShot();
  hasher.addBool(lastShot.has_value());
  if (lastShot) {
    addShot(hasher, *lastShot);
  }
  const auto& reception = state.lastReception();
  hasher.addBool(reception.has_value());
  if (reception) {
    hasher.addU64(reception->player.value());
    hasher.addI64(reception->tick.value());
    hasher.addDouble(reception->ballSpeed);
    hasher.addBool(reception->passer.has_value());
    if (reception->passer) {
      hasher.addU64(reception->passer->value());
    }
  }
  hasher.addI64(state.score().home);
  hasher.addI64(state.score().away);
  const auto& lastGoal = state.lastGoal();
  hasher.addBool(lastGoal.has_value());
  if (lastGoal) {
    hasher.addU64(static_cast<std::uint64_t>(lastGoal->side));
    hasher.addBool(lastGoal->scorer.has_value());
    hasher.addU64(lastGoal->scorer.value_or(SimCore::PlayerId::invalid()).value());
    hasher.addI64(lastGoal->tick.value());
  }
  const MatchClock& clock = state.clock();
  hasher.addU64(static_cast<std::uint64_t>(clock.period));
  hasher.addI64(clock.periodStart.value());
  hasher.addDouble(clock.lostSeconds);
  hasher.addBool(clock.addedMinutes.has_value());
  hasher.addI64(clock.addedMinutes.value_or(0));
  addOffside(hasher, state);
  addSetPiece(hasher, state.setPiece());
  addIndirectKick(hasher, state.indirectKick());
  addSubstitutions(hasher, state.pendingSubstitutions());
  addSubstitutions(hasher, state.substitutions());
  addPendingTactics(hasher, state);
  const auto& foul = state.lastFoul();
  hasher.addBool(foul.has_value());
  if (foul) {
    hasher.addU64(foul->offender.value());
    hasher.addU64(foul->victim.value());
    hasher.addI64(foul->tick.value());
    addVec2(hasher, foul->position);
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
  for (const TeamSide side : {TeamSide::kHome, TeamSide::kAway}) {
    addPress(hasher, state.press(side));
  }
  return hasher.value();
}

}  // namespace ElyverseFootball::SimMatch
