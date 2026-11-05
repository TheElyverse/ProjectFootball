#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include "tactic.hpp"

namespace ElyverseFootball::SimTactics {

// Familiar eleven-a-side formations, as editing help like the role presets
// (docs/tactics.md#formation-presets): a preset only fills in eleven slots,
// each a position in the base shape and a role preset's responsibilities.
// Nothing in the match knows which formation a tactic came from.
enum class Formation : std::uint8_t {
  k442,
  k433,
};

inline constexpr std::size_t kFormationCount = 2;
inline constexpr std::array<Formation, kFormationCount> kAllFormations{Formation::k442,
                                                                       Formation::k433};

// "4-4-2", "4-3-3"; "unknown" for a value outside the enumerators.
[[nodiscard]] std::string_view formationName(Formation formation) noexcept;
[[nodiscard]] std::optional<Formation> parseFormation(std::string_view name) noexcept;

// The formation's eleven slots in line-up order: the goalkeeper, the back
// four from the touchline at y = 0 to the other, then midfield and attack the
// same way. Empty for a value outside the enumerators.
[[nodiscard]] std::vector<TacticSlot> formationSlots(Formation formation);

}  // namespace ElyverseFootball::SimTactics
