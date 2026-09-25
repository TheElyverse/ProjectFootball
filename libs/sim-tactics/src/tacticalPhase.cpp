#include "tacticalPhase.hpp"

namespace ElyverseFootball::SimTactics {

std::string_view phaseName(const TacticalPhase phase) noexcept {
  switch (phase) {
    case TacticalPhase::kBuildUp:
      return "buildUp";
    case TacticalPhase::kProgression:
      return "progression";
    case TacticalPhase::kFinalThird:
      return "finalThird";
    case TacticalPhase::kAttackingTransition:
      return "attackingTransition";
    case TacticalPhase::kDefensiveBlock:
      return "defensiveBlock";
    case TacticalPhase::kPressing:
      return "pressing";
    case TacticalPhase::kDefensiveTransition:
      return "defensiveTransition";
  }
  return "unknown";
}

std::optional<TacticalPhase> parsePhase(const std::string_view name) noexcept {
  for (const TacticalPhase phase : kAllPhases) {
    if (phaseName(phase) == name) {
      return phase;
    }
  }
  return std::nullopt;
}

}  // namespace ElyverseFootball::SimTactics
