#include "responsibility.hpp"

#include <algorithm>

namespace ElyverseFootball::SimTactics {

std::string_view responsibilityName(const Responsibility responsibility) noexcept {
  switch (responsibility) {
    case Responsibility::kGuardGoal:
      return "guardGoal";
    case Responsibility::kHoldDefensiveLine:
      return "holdDefensiveLine";
    case Responsibility::kHoldRestDefence:
      return "holdRestDefence";
    case Responsibility::kProvideWidth:
      return "provideWidth";
    case Responsibility::kOccupyHalfspace:
      return "occupyHalfspace";
    case Responsibility::kSupportCarrier:
      return "supportCarrier";
    case Responsibility::kRunInBehind:
      return "runInBehind";
    case Responsibility::kMarkOpponent:
      return "markOpponent";
    case Responsibility::kCover:
      return "cover";
    case Responsibility::kClosePressingLine:
      return "closePressingLine";
  }
  return "unknown";
}

std::optional<Responsibility> parseResponsibility(const std::string_view name) noexcept {
  for (const Responsibility responsibility : kAllResponsibilities) {
    if (responsibilityName(responsibility) == name) {
      return responsibility;
    }
  }
  return std::nullopt;
}

bool isKnown(const Responsibility responsibility) noexcept {
  return std::ranges::find(kAllResponsibilities, responsibility) != kAllResponsibilities.end();
}

bool contradicts(const Responsibility first, const Responsibility second) noexcept {
  if (first == second) {
    return false;
  }
  if (first == Responsibility::kGuardGoal || second == Responsibility::kGuardGoal) {
    return true;
  }
  const auto isPair = [first, second](const Responsibility left, const Responsibility right) {
    return (first == left && second == right) || (first == right && second == left);
  };
  return isPair(Responsibility::kProvideWidth, Responsibility::kOccupyHalfspace) ||
         isPair(Responsibility::kRunInBehind, Responsibility::kHoldRestDefence) ||
         isPair(Responsibility::kRunInBehind, Responsibility::kHoldDefensiveLine);
}

std::string_view pressingTriggerName(const PressingTrigger trigger) noexcept {
  switch (trigger) {
    case PressingTrigger::kPoorFirstTouch:
      return "poorFirstTouch";
    case PressingTrigger::kBackPass:
      return "backPass";
    case PressingTrigger::kReceiverFacingOwnGoal:
      return "receiverFacingOwnGoal";
    case PressingTrigger::kIsolatedReceiver:
      return "isolatedReceiver";
    case PressingTrigger::kSlowPass:
      return "slowPass";
  }
  return "unknown";
}

std::optional<PressingTrigger> parsePressingTrigger(const std::string_view name) noexcept {
  for (const PressingTrigger trigger : kAllPressingTriggers) {
    if (pressingTriggerName(trigger) == name) {
      return trigger;
    }
  }
  return std::nullopt;
}

bool isKnown(const PressingTrigger trigger) noexcept {
  return std::ranges::find(kAllPressingTriggers, trigger) != kAllPressingTriggers.end();
}

}  // namespace ElyverseFootball::SimTactics
