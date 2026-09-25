#include "rolePreset.hpp"

namespace ElyverseFootball::SimTactics {

std::string_view rolePresetName(const RolePreset preset) noexcept {
  switch (preset) {
    case RolePreset::kGoalkeeper:
      return "goalkeeper";
    case RolePreset::kCentreBack:
      return "centreBack";
    case RolePreset::kFullBack:
      return "fullBack";
    case RolePreset::kHoldingMidfielder:
      return "holdingMidfielder";
    case RolePreset::kCentralMidfielder:
      return "centralMidfielder";
    case RolePreset::kWinger:
      return "winger";
    case RolePreset::kInsideForward:
      return "insideForward";
    case RolePreset::kStriker:
      return "striker";
  }
  return "unknown";
}

std::optional<RolePreset> parseRolePreset(const std::string_view name) noexcept {
  for (const RolePreset preset : kAllRolePresets) {
    if (rolePresetName(preset) == name) {
      return preset;
    }
  }
  return std::nullopt;
}

std::vector<SlotResponsibility> presetResponsibilities(const RolePreset preset) {
  using enum Responsibility;
  switch (preset) {
    case RolePreset::kGoalkeeper:
      return {{.responsibility = kGuardGoal, .weight = 1.0}};
    case RolePreset::kCentreBack:
      return {{.responsibility = kHoldDefensiveLine, .weight = 1.0},
              {.responsibility = kMarkOpponent, .weight = 0.6},
              {.responsibility = kCover, .weight = 0.6}};
    case RolePreset::kFullBack:
      return {{.responsibility = kHoldDefensiveLine, .weight = 1.0},
              {.responsibility = kProvideWidth, .weight = 0.6},
              {.responsibility = kMarkOpponent, .weight = 0.5}};
    case RolePreset::kHoldingMidfielder:
      return {{.responsibility = kHoldRestDefence, .weight = 1.0},
              {.responsibility = kCover, .weight = 0.8},
              {.responsibility = kSupportCarrier, .weight = 0.5}};
    case RolePreset::kCentralMidfielder:
      return {{.responsibility = kSupportCarrier, .weight = 1.0},
              {.responsibility = kClosePressingLine, .weight = 0.6},
              {.responsibility = kOccupyHalfspace, .weight = 0.4}};
    case RolePreset::kWinger:
      return {{.responsibility = kProvideWidth, .weight = 1.0},
              {.responsibility = kRunInBehind, .weight = 0.5},
              {.responsibility = kClosePressingLine, .weight = 0.5}};
    case RolePreset::kInsideForward:
      return {{.responsibility = kOccupyHalfspace, .weight = 1.0},
              {.responsibility = kRunInBehind, .weight = 0.7},
              {.responsibility = kClosePressingLine, .weight = 0.6}};
    case RolePreset::kStriker:
      return {{.responsibility = kRunInBehind, .weight = 1.0},
              {.responsibility = kClosePressingLine, .weight = 1.0},
              {.responsibility = kSupportCarrier, .weight = 0.4}};
  }
  return {};
}

}  // namespace ElyverseFootball::SimTactics
