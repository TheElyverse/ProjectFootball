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
    case ActionType::kMarkOpponent:
      return "markOpponent";
    case ActionType::kTrackRunner:
      return "trackRunner";
    case ActionType::kCover:
      return "cover";
    case ActionType::kPressCarrier:
      return "pressCarrier";
    case ActionType::kBlockLane:
      return "blockLane";
  }
  return "unknown";
}

}  // namespace ElyverseFootball::SimMatch
