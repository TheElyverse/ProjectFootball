#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace ElyverseFootball::SimTactics {

// The situation a team is in, as its tactic sees it (docs/tactics.md). Four
// phases with the ball, three without. A tactic gives instructions per phase;
// the match decides which phase each team is in.
enum class TacticalPhase : std::uint8_t {
  // In possession, the ball in the own third.
  kBuildUp,
  // In possession, the ball in the middle third.
  kProgression,
  // In possession, the ball in the opponent's third.
  kFinalThird,
  // Just won the ball: the moment to attack before the opponent is organized.
  kAttackingTransition,
  // Out of possession, defending in a block.
  kDefensiveBlock,
  // Out of possession, the ball high enough up the pitch to press.
  kPressing,
  // Just lost the ball: the moment to win it back or get behind it.
  kDefensiveTransition,
};

inline constexpr std::size_t kPhaseCount = 7;

// Every phase in declaration order, for loops and tables indexed by phase.
inline constexpr std::array<TacticalPhase, kPhaseCount> kAllPhases{
    TacticalPhase::kBuildUp,
    TacticalPhase::kProgression,
    TacticalPhase::kFinalThird,
    TacticalPhase::kAttackingTransition,
    TacticalPhase::kDefensiveBlock,
    TacticalPhase::kPressing,
    TacticalPhase::kDefensiveTransition,
};

// The phase's position in kAllPhases.
[[nodiscard]] constexpr std::size_t phaseIndex(const TacticalPhase phase) noexcept {
  return static_cast<std::size_t>(phase);
}

// Whether a team in this phase has the ball.
[[nodiscard]] constexpr bool isInPossession(const TacticalPhase phase) noexcept {
  return phase == TacticalPhase::kBuildUp || phase == TacticalPhase::kProgression ||
         phase == TacticalPhase::kFinalThird || phase == TacticalPhase::kAttackingTransition;
}

// "buildUp", "progression", ... as tactic files spell them; "unknown" for a
// value outside the enumerators.
[[nodiscard]] std::string_view phaseName(TacticalPhase phase) noexcept;

// The phase a name from phaseName() stands for; empty for any other text.
[[nodiscard]] std::optional<TacticalPhase> parsePhase(std::string_view name) noexcept;

}  // namespace ElyverseFootball::SimTactics
