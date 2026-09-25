#include "referenceTactic.hpp"

#include "rolePreset.hpp"

namespace ElyverseFootball::SimTactics {
namespace {

[[nodiscard]] TacticSlot slot(const double depth, const double width, const RolePreset preset) {
  return {.position = {.depth = depth, .width = width},
          .responsibilities = presetResponsibilities(preset)};
}

}  // namespace

TacticSpec referenceTacticSpec() {
  TacticSpec spec{
      .name = "reference",
      .slots = {slot(0.04, 0.50, RolePreset::kGoalkeeper),
                slot(0.20, 0.30, RolePreset::kCentreBack),
                slot(0.20, 0.70, RolePreset::kCentreBack),
                slot(0.35, 0.50, RolePreset::kHoldingMidfielder),
                slot(0.50, 0.12, RolePreset::kWinger), slot(0.50, 0.88, RolePreset::kWinger),
                slot(0.65, 0.50, RolePreset::kStriker)},
      .principles = {},
      .phases = {}};
  // With the ball the block moves up and spreads out; without it, it drops
  // and narrows. Transitions keep the shape of the phase they lead to.
  const PhaseInstruction attacking{.lineHeight = 0.35,
                                   .blockLength = 0.45,
                                   .blockWidth = 0.85,
                                   .ballShift = 0.4,
                                   .pressingIntensity = 0.3,
                                   .passingRisk = 0.5,
                                   .runFrequency = 0.3};
  const PhaseInstruction defending{.lineHeight = 0.25,
                                   .blockLength = 0.4,
                                   .blockWidth = 0.6,
                                   .ballShift = 0.5,
                                   .pressingIntensity = 0.3,
                                   .passingRisk = 0.5,
                                   .runFrequency = 0.3};
  for (const TacticalPhase phase : kAllPhases) {
    spec.phases.at(phaseIndex(phase)) = isInPossession(phase) ? attacking : defending;
  }
  return spec;
}

}  // namespace ElyverseFootball::SimTactics
