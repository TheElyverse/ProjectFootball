#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include "responsibility.hpp"

namespace ElyverseFootball::SimTactics {

// Familiar football roles, as editing help: a preset only fills in a slot's
// atomic responsibilities (implementation plan section 7.1). Nothing in the
// match knows which preset a slot came from.
enum class RolePreset : std::uint8_t {
  kGoalkeeper,
  kCentreBack,
  kFullBack,
  kHoldingMidfielder,
  kCentralMidfielder,
  kWinger,
  kInsideForward,
  kStriker,
};

inline constexpr std::size_t kRolePresetCount = 8;

inline constexpr std::array<RolePreset, kRolePresetCount> kAllRolePresets{
    RolePreset::kGoalkeeper,        RolePreset::kCentreBack,        RolePreset::kFullBack,
    RolePreset::kHoldingMidfielder, RolePreset::kCentralMidfielder, RolePreset::kWinger,
    RolePreset::kInsideForward,     RolePreset::kStriker,
};

// "goalkeeper", "centreBack", ... as tactic files spell them; "unknown" for a
// value outside the enumerators.
[[nodiscard]] std::string_view rolePresetName(RolePreset preset) noexcept;

[[nodiscard]] std::optional<RolePreset> parseRolePreset(std::string_view name) noexcept;

// The responsibilities the preset stands for, primary duty first. Each list
// is valid for a slot: known responsibilities, weights in (0, 1], no
// duplicates and no contradictions. Empty for a value outside the
// enumerators.
[[nodiscard]] std::vector<SlotResponsibility> presetResponsibilities(RolePreset preset);

}  // namespace ElyverseFootball::SimTactics
