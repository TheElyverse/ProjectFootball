#include "tactic.hpp"

#include <algorithm>
#include <format>
#include <string_view>
#include <utility>

namespace ElyverseFootball::SimTactics {
namespace {

// In [min, max], and false for NaN, so one check covers range and finiteness.
[[nodiscard]] bool isWithin(const double value, const double min, const double max) noexcept {
  return value >= min && value <= max;
}

// In (0, max], and false for NaN.
[[nodiscard]] bool isPositiveUpTo(const double value, const double max) noexcept {
  return value > 0.0 && value <= max;
}

class ErrorList {
 public:
  void add(const TacticErrorCode code, std::string field, std::string message) {
    errors_.push_back({.code = code, .field = std::move(field), .message = std::move(message)});
  }

  // A value that must lie in [min, max].
  void checkRange(const double value, const double min, const double max, std::string field) {
    if (!isWithin(value, min, max)) {
      std::string message = std::format("{} must lie in [{}, {}]", value, min, max);
      add(TacticErrorCode::kValueOutOfRange, std::move(field), std::move(message));
    }
  }

  // A value that must lie in (0, max]: a length or width of zero is no block.
  void checkPositive(const double value, const double max, std::string field) {
    if (!isPositiveUpTo(value, max)) {
      std::string message = std::format("{} must lie in (0, {}]", value, max);
      add(TacticErrorCode::kValueOutOfRange, std::move(field), std::move(message));
    }
  }

  [[nodiscard]] std::vector<TacticError> take() && { return std::move(errors_); }
  [[nodiscard]] bool empty() const noexcept { return errors_.empty(); }

 private:
  std::vector<TacticError> errors_;
};

void validateResponsibilities(const std::size_t slotIndex, const TacticSlot& slot,
                              ErrorList& errors) {
  const std::string slotField = std::format("slots[{}].responsibilities", slotIndex);
  const auto& responsibilities = slot.responsibilities;
  for (std::size_t index = 0; index < responsibilities.size(); ++index) {
    const SlotResponsibility& entry = responsibilities[index];
    const std::string field = std::format("{}[{}]", slotField, index);
    if (!isKnown(entry.responsibility)) {
      errors.add(TacticErrorCode::kUnknownResponsibility, field,
                 std::format("unknown responsibility {}", static_cast<int>(entry.responsibility)));
      continue;
    }
    if (!isPositiveUpTo(entry.weight, 1.0)) {
      errors.add(TacticErrorCode::kInvalidResponsibilityWeight, field + ".weight",
                 std::format("weight {} of {} must lie in (0, 1]", entry.weight,
                             responsibilityName(entry.responsibility)));
    }
    for (std::size_t earlier = 0; earlier < index; ++earlier) {
      const Responsibility other = responsibilities[earlier].responsibility;
      if (other == entry.responsibility) {
        errors.add(TacticErrorCode::kDuplicateResponsibility, field,
                   std::format("{} is listed twice", responsibilityName(entry.responsibility)));
      } else if (contradicts(other, entry.responsibility)) {
        errors.add(TacticErrorCode::kContradictoryResponsibilities, field,
                   std::format("{} contradicts {}", responsibilityName(entry.responsibility),
                               responsibilityName(other)));
      }
    }
  }
}

void validateSlots(const std::vector<TacticSlot>& slots, ErrorList& errors) {
  if (slots.size() != kSlotsPerTactic) {
    errors.add(
        TacticErrorCode::kWrongSlotCount, "slots",
        std::format("has {} slots, a seven-a-side tactic needs {}", slots.size(), kSlotsPerTactic));
  }
  std::size_t goalkeepers = 0;
  for (std::size_t index = 0; index < slots.size(); ++index) {
    const TacticSlot& slot = slots[index];
    const auto checkOnPitch = [&](const double value, const std::string_view axis) {
      if (!isWithin(value, 0.0, 1.0)) {
        errors.add(TacticErrorCode::kSlotOutsidePitch,
                   std::format("slots[{}].position.{}", index, axis),
                   std::format("{} lies off the pitch; expected a fraction in [0, 1]", value));
      }
    };
    checkOnPitch(slot.position.depth, "depth");
    checkOnPitch(slot.position.width, "width");
    validateResponsibilities(index, slot, errors);
    const bool keeper = std::ranges::any_of(slot.responsibilities, [](const auto& entry) {
      return entry.responsibility == Responsibility::kGuardGoal;
    });
    if (keeper && ++goalkeepers > 1) {
      errors.add(TacticErrorCode::kTooManyGoalkeepers,
                 std::format("slots[{}].responsibilities", index),
                 "a second slot guards the goal; a tactic has at most one goalkeeper");
    }
  }
}

void validatePrinciples(const TeamPrinciples& principles, ErrorList& errors) {
  errors.checkRange(principles.pressingLine, 0.0, 1.0, "principles.pressingLine");
  const auto& triggers = principles.pressingTriggers;
  for (std::size_t index = 0; index < triggers.size(); ++index) {
    const std::string field = std::format("principles.pressingTriggers[{}]", index);
    if (!isKnown(triggers[index])) {
      errors.add(TacticErrorCode::kUnknownPressingTrigger, field,
                 std::format("unknown pressing trigger {}", static_cast<int>(triggers[index])));
    } else if (std::find(triggers.begin(), triggers.begin() + static_cast<std::ptrdiff_t>(index),
                         triggers[index]) !=
               triggers.begin() + static_cast<std::ptrdiff_t>(index)) {
      errors.add(TacticErrorCode::kDuplicatePressingTrigger, field,
                 std::format("{} is listed twice", pressingTriggerName(triggers[index])));
    }
  }
  const PositioningWeights& weights = principles.positioning;
  const std::string prefix = "principles.positioning.";
  errors.checkRange(weights.targetDistance, 0.0, kMaxPositioningWeight, prefix + "targetDistance");
  errors.checkRange(weights.spacing, 0.0, kMaxPositioningWeight, prefix + "spacing");
  errors.checkRange(weights.pressure, 0.0, kMaxPositioningWeight, prefix + "pressure");
  errors.checkRange(weights.occupancy, 0.0, kMaxPositioningWeight, prefix + "occupancy");
  errors.checkRange(weights.transitionRisk, 0.0, kMaxPositioningWeight, prefix + "transitionRisk");
}

void validatePhase(const TacticalPhase phase, const PhaseInstruction& instruction,
                   ErrorList& errors) {
  const std::string prefix = std::format("phases.{}.", phaseName(phase));
  errors.checkRange(instruction.lineHeight, 0.0, 1.0, prefix + "lineHeight");
  errors.checkPositive(instruction.blockLength, 1.0, prefix + "blockLength");
  errors.checkPositive(instruction.blockWidth, 1.0, prefix + "blockWidth");
  errors.checkRange(instruction.ballShift, 0.0, 1.0, prefix + "ballShift");
  errors.checkRange(instruction.pressingIntensity, 0.0, 1.0, prefix + "pressingIntensity");
  errors.checkRange(instruction.passingRisk, 0.0, 1.0, prefix + "passingRisk");
  errors.checkRange(instruction.runFrequency, 0.0, 1.0, prefix + "runFrequency");
  // Each value may be fine on its own and the two still describe no block on
  // the pitch.
  if (instruction.lineHeight + instruction.blockLength > 1.0) {
    errors.add(TacticErrorCode::kContradictoryParameters, prefix + "blockLength",
               std::format("a block of length {} from a line at {} reaches past the opponent's "
                           "goal line",
                           instruction.blockLength, instruction.lineHeight));
  }
}

}  // namespace

std::expected<Tactic, std::vector<TacticError>> Tactic::create(TacticSpec spec) {
  ErrorList errors;
  if (spec.name.empty()) {
    errors.add(TacticErrorCode::kEmptyName, "name", "a tactic needs a name");
  }
  validateSlots(spec.slots, errors);
  validatePrinciples(spec.principles, errors);
  for (const TacticalPhase phase : kAllPhases) {
    validatePhase(phase, spec.phases.at(phaseIndex(phase)), errors);
  }
  if (!errors.empty()) {
    return std::unexpected(std::move(errors).take());
  }
  return Tactic(std::move(spec));
}

double Tactic::responsibilityWeight(const std::size_t slot,
                                    const Responsibility responsibility) const {
  for (const SlotResponsibility& entry : spec_.slots.at(slot).responsibilities) {
    if (entry.responsibility == responsibility) {
      return entry.weight;
    }
  }
  return 0.0;
}

bool Tactic::pressesOn(const PressingTrigger trigger) const noexcept {
  return std::ranges::find(spec_.principles.pressingTriggers, trigger) !=
         spec_.principles.pressingTriggers.end();
}

}  // namespace ElyverseFootball::SimTactics
