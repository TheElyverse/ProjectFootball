#include "tacticHash.hpp"

namespace ElyverseFootball::SimTactics {
namespace {

using SimCore::StableHasher;

// Tactic::operator== compares doubles with ==, under which -0.0 equals 0.0,
// but StableHasher::addDouble hashes their distinct bit patterns; normalize
// signed zero so equal tactics keep hashing alike.
[[nodiscard]] double normalizeZero(const double value) noexcept {
  return value == 0.0 ? 0.0 : value;
}

void addHashedDouble(StableHasher& hasher, const double value) noexcept {
  hasher.addDouble(normalizeZero(value));
}

void addInstruction(StableHasher& hasher, const PhaseInstruction& instruction) noexcept {
  addHashedDouble(hasher, instruction.lineHeight);
  addHashedDouble(hasher, instruction.blockLength);
  addHashedDouble(hasher, instruction.blockWidth);
  addHashedDouble(hasher, instruction.ballShift);
  addHashedDouble(hasher, instruction.pressingIntensity);
  addHashedDouble(hasher, instruction.passingRisk);
  addHashedDouble(hasher, instruction.runFrequency);
}

void addPrinciples(StableHasher& hasher, const TeamPrinciples& principles) noexcept {
  addHashedDouble(hasher, principles.pressingLine);
  hasher.addU64(principles.pressingTriggers.size());
  for (const PressingTrigger trigger : principles.pressingTriggers) {
    hasher.addByte(static_cast<std::uint8_t>(trigger));
  }
  const PositioningWeights& weights = principles.positioning;
  addHashedDouble(hasher, weights.targetDistance);
  addHashedDouble(hasher, weights.spacing);
  addHashedDouble(hasher, weights.pressure);
  addHashedDouble(hasher, weights.occupancy);
  addHashedDouble(hasher, weights.transitionRisk);
}

}  // namespace

void addTactic(StableHasher& hasher, const Tactic& tactic) noexcept {
  hasher.addString(tactic.name());
  hasher.addString(tactic.description());
  hasher.addU64(tactic.slots().size());
  for (const TacticSlot& slot : tactic.slots()) {
    addHashedDouble(hasher, slot.position.depth);
    addHashedDouble(hasher, slot.position.width);
    hasher.addU64(slot.responsibilities.size());
    for (const SlotResponsibility& entry : slot.responsibilities) {
      hasher.addByte(static_cast<std::uint8_t>(entry.responsibility));
      addHashedDouble(hasher, entry.weight);
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
