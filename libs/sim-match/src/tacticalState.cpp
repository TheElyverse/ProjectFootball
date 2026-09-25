#include "tacticalState.hpp"

namespace ElyverseFootball::SimMatch {

std::string_view actionName(const ActionType type) noexcept {
  switch (type) {
    case ActionType::kHoldPosition:
      return "holdPosition";
    case ActionType::kSupportCarrier:
      return "supportCarrier";
    case ActionType::kMoveIntoSpace:
      return "moveIntoSpace";
    case ActionType::kRunInBehind:
      return "runInBehind";
    case ActionType::kCreateWidth:
      return "createWidth";
    case ActionType::kOccupyHalfspace:
      return "occupyHalfspace";
  }
  return "unknown";
}

}  // namespace ElyverseFootball::SimMatch
