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

std::string_view carryKindName(const CarryKind kind) noexcept {
  switch (kind) {
    case CarryKind::kHold:
      return "hold";
    case CarryKind::kTurn:
      return "turn";
    case CarryKind::kCarry:
      return "carry";
    case CarryKind::kDribble:
      return "dribble";
  }
  return "unknown";
}

std::string_view duelMoveName(const DuelMove move) noexcept {
  switch (move) {
    case DuelMove::kShield:
      return "shield";
    case DuelMove::kTurnAway:
      return "turnAway";
    case DuelMove::kAccelerate:
      return "accelerate";
    case DuelMove::kJockey:
      return "jockey";
    case DuelMove::kCommit:
      return "commit";
  }
  return "unknown";
}

bool isStumbling(const PlayerTacticalState& tactical, const SimCore::SimTick now) noexcept {
  return tactical.stumbledUntil && now < *tactical.stumbledUntil;
}

bool isBusy(const KeeperDive& dive, const double seconds) noexcept {
  return seconds < dive.landSeconds + dive.recoverySeconds;
}

bool isDiving(const PlayerTacticalState& tactical, const SimCore::SimTick now,
              const double secondsPerTick) noexcept {
  const auto& dive = tactical.dive;
  return dive &&
         isBusy(*dive, static_cast<double>(now.value() - dive->tick.value()) * secondsPerTick);
}

bool isOffHisFeet(const PlayerTacticalState& tactical, const SimCore::SimTick now,
                  const double secondsPerTick) noexcept {
  return isStumbling(tactical, now) || isDiving(tactical, now, secondsPerTick);
}

}  // namespace ElyverseFootball::SimMatch
