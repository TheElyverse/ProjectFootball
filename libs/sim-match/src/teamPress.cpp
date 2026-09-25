#include "teamPress.hpp"

namespace ElyverseFootball::SimMatch {

std::string_view pressRoleName(const PressRole role) noexcept {
  switch (role) {
    case PressRole::kPress:
      return "press";
    case PressRole::kBlockLane:
      return "blockLane";
    case PressRole::kCover:
      return "cover";
  }
  return "unknown";
}

std::string_view pressOutcomeName(const PressOutcome outcome) noexcept {
  switch (outcome) {
    case PressOutcome::kBallRegained:
      return "ballRegained";
    case PressOutcome::kPassedOut:
      return "passedOut";
    case PressOutcome::kCarrierEscaped:
      return "carrierEscaped";
  }
  return "unknown";
}

}  // namespace ElyverseFootball::SimMatch
