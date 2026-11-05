#include "setPiecePlan.hpp"

namespace ElyverseFootball::SimTactics {

std::string_view setPieceKindName(const SetPieceKind kind) noexcept {
  switch (kind) {
    case SetPieceKind::kKickoff:
      return "kickoff";
    case SetPieceKind::kThrowIn:
      return "throwIn";
    case SetPieceKind::kCorner:
      return "corner";
    case SetPieceKind::kGoalKick:
      return "goalKick";
    case SetPieceKind::kFreeKick:
      return "freeKick";
    case SetPieceKind::kPenalty:
      return "penalty";
  }
  return "unknown";
}

std::string_view setPieceDeliveryName(const SetPieceDelivery delivery) noexcept {
  switch (delivery) {
    case SetPieceDelivery::kShort:
      return "short";
    case SetPieceDelivery::kNearPost:
      return "nearPost";
    case SetPieceDelivery::kFarPost:
      return "farPost";
    case SetPieceDelivery::kPenaltySpot:
      return "penaltySpot";
    case SetPieceDelivery::kLong:
      return "long";
  }
  return "unknown";
}

std::optional<SetPieceDelivery> parseSetPieceDelivery(const std::string_view name) noexcept {
  for (const SetPieceDelivery delivery :
       {SetPieceDelivery::kShort, SetPieceDelivery::kNearPost, SetPieceDelivery::kFarPost,
        SetPieceDelivery::kPenaltySpot, SetPieceDelivery::kLong}) {
    if (setPieceDeliveryName(delivery) == name) {
      return delivery;
    }
  }
  return std::nullopt;
}

bool deliveryFits(const SetPieceKind kind, const SetPieceDelivery delivery) noexcept {
  if (kind == SetPieceKind::kCorner) {
    return delivery != SetPieceDelivery::kLong;
  }
  if (kind == SetPieceKind::kFreeKick || kind == SetPieceKind::kPenalty) {
    return true;
  }
  return delivery == SetPieceDelivery::kShort || delivery == SetPieceDelivery::kLong;
}

const SetPiecePlan& SetPiecePlans::of(const SetPieceKind kind) const noexcept {
  switch (kind) {
    case SetPieceKind::kKickoff:
      return kickoff;
    case SetPieceKind::kThrowIn:
      return throwIn;
    case SetPieceKind::kCorner:
      return corner;
    case SetPieceKind::kGoalKick:
      return goalKick;
    case SetPieceKind::kFreeKick:
      return freeKick;
    case SetPieceKind::kPenalty:
      return penalty;
  }
  return kickoff;
}

SetPiecePlan& SetPiecePlans::of(const SetPieceKind kind) noexcept {
  switch (kind) {
    case SetPieceKind::kKickoff:
      return kickoff;
    case SetPieceKind::kThrowIn:
      return throwIn;
    case SetPieceKind::kCorner:
      return corner;
    case SetPieceKind::kGoalKick:
      return goalKick;
    case SetPieceKind::kFreeKick:
      return freeKick;
    case SetPieceKind::kPenalty:
      return penalty;
  }
  return kickoff;
}

}  // namespace ElyverseFootball::SimTactics
