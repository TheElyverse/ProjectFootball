#include "formation.hpp"

#include "rolePreset.hpp"

namespace ElyverseFootball::SimTactics {
namespace {

[[nodiscard]] TacticSlot slot(const double depth, const double width, const RolePreset preset) {
  return {.position = {.depth = depth, .width = width},
          .responsibilities = presetResponsibilities(preset)};
}

// The goalkeeper and the back four every preset starts from.
[[nodiscard]] std::vector<TacticSlot> backFive() {
  return {slot(0.04, 0.50, RolePreset::kGoalkeeper), slot(0.20, 0.12, RolePreset::kFullBack),
          slot(0.18, 0.38, RolePreset::kCentreBack), slot(0.18, 0.62, RolePreset::kCentreBack),
          slot(0.20, 0.88, RolePreset::kFullBack)};
}

}  // namespace

std::string_view formationName(const Formation formation) noexcept {
  switch (formation) {
    case Formation::k442:
      return "4-4-2";
    case Formation::k433:
      return "4-3-3";
  }
  return "unknown";
}

std::optional<Formation> parseFormation(const std::string_view name) noexcept {
  for (const Formation formation : kAllFormations) {
    if (formationName(formation) == name) {
      return formation;
    }
  }
  return std::nullopt;
}

std::vector<TacticSlot> formationSlots(const Formation formation) {
  std::vector<TacticSlot> slots = backFive();
  switch (formation) {
    case Formation::k442:
      slots.push_back(slot(0.42, 0.12, RolePreset::kWinger));
      slots.push_back(slot(0.36, 0.38, RolePreset::kHoldingMidfielder));
      slots.push_back(slot(0.38, 0.62, RolePreset::kCentralMidfielder));
      slots.push_back(slot(0.42, 0.88, RolePreset::kWinger));
      slots.push_back(slot(0.62, 0.40, RolePreset::kStriker));
      slots.push_back(slot(0.62, 0.60, RolePreset::kStriker));
      return slots;
    case Formation::k433:
      slots.push_back(slot(0.42, 0.32, RolePreset::kCentralMidfielder));
      slots.push_back(slot(0.32, 0.50, RolePreset::kHoldingMidfielder));
      slots.push_back(slot(0.42, 0.68, RolePreset::kCentralMidfielder));
      slots.push_back(slot(0.60, 0.12, RolePreset::kWinger));
      slots.push_back(slot(0.66, 0.50, RolePreset::kStriker));
      slots.push_back(slot(0.60, 0.88, RolePreset::kWinger));
      return slots;
  }
  return {};
}

}  // namespace ElyverseFootball::SimTactics
