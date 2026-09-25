#include "tacticHash.hpp"

namespace ElyverseFootball::SimTactics {
namespace {

using SimCore::StableHasher;

void addInstruction(StableHasher& hasher, const PhaseInstruction& instruction) noexcept {
  hasher.addDouble(instruction.lineHeight);
  hasher.addDouble(instruction.blockLength);
  hasher.addDouble(instruction.blockWidth);
  hasher.addDouble(instruction.ballShift);
  hasher.addDouble(instruction.pressingIntensity);
  hasher.addDouble(instruction.passingRisk);
  hasher.addDouble(instruction.runFrequency);
}

void addPrinciples(StableHasher& hasher, const TeamPrinciples& principles) noexcept {
  hasher.addDouble(principles.pressingLine);
  hasher.addU64(principles.pressingTriggers.size());
  for (const PressingTrigger trigger : principles.pressingTriggers) {
    hasher.addByte(static_cast<std::uint8_t>(trigger));
  }
  const PositioningWeights& weights = principles.positioning;
  hasher.addDouble(weights.targetDistance);
  hasher.addDouble(weights.spacing);
  hasher.addDouble(weights.pressure);
  hasher.addDouble(weights.occupancy);
  hasher.addDouble(weights.transitionRisk);
}

}  // namespace

void addTactic(StableHasher& hasher, const Tactic& tactic) noexcept {
  hasher.addString(tactic.name());
  hasher.addString(tactic.description());
  hasher.addU64(tactic.slots().size());
  for (const TacticSlot& slot : tactic.slots()) {
    hasher.addDouble(slot.position.depth);
    hasher.addDouble(slot.position.width);
    hasher.addU64(slot.responsibilities.size());
    for (const SlotResponsibility& entry : slot.responsibilities) {
      hasher.addByte(static_cast<std::uint8_t>(entry.responsibility));
      hasher.addDouble(entry.weight);
    }
  }
  addPrinciples(hasher, tactic.principles());
  for (const PhaseInstruction& instruction : tactic.spec().phases) {
    addInstruction(hasher, instruction);
  }
}

std::uint64_t contentHash(const Tactic& tactic) noexcept {
  StableHasher hasher;
  addTactic(hasher, tactic);
  return hasher.value();
}

}  // namespace ElyverseFootball::SimTactics
