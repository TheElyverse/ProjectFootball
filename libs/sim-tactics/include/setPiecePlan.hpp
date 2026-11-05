#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace ElyverseFootball::SimTactics {

// The set pieces a tactic plans for (docs/set-pieces.md#plans).
enum class SetPieceKind : std::uint8_t {
  kKickoff,
  kThrowIn,
  kCorner,
  kGoalKick,
};

inline constexpr std::size_t kSetPieceKindCount = 4;

// "kickoff", "throwIn", "corner", "goalKick"; "unknown" outside the
// enumerators.
[[nodiscard]] std::string_view setPieceKindName(SetPieceKind kind) noexcept;

// What a set piece's delivery aims at: a teammate close by, the near post, the
// far post or the penalty spot -- a corner's areas --, or a teammate far up the
// pitch.
enum class SetPieceDelivery : std::uint8_t {
  kShort,
  kNearPost,
  kFarPost,
  kPenaltySpot,
  kLong,
};

// "short", "nearPost", "farPost", "penaltySpot", "long"; "unknown" outside the
// enumerators.
[[nodiscard]] std::string_view setPieceDeliveryName(SetPieceDelivery delivery) noexcept;

// The delivery of this name, empty for an unknown one.
[[nodiscard]] std::optional<SetPieceDelivery> parseSetPieceDelivery(std::string_view name) noexcept;

// Whether a set piece of this kind can aim at this delivery: a corner at the
// short option or one of its areas, the others short or long.
[[nodiscard]] bool deliveryFits(SetPieceKind kind, SetPieceDelivery delivery) noexcept;

// One set piece as the side plans it (docs/set-pieces.md#plans): who takes
// it, what the delivery aims at, which slots go forward to receive it when
// the side takes it, and which mark the opponent's runners man for man when
// the opponent does. Slots are indices into the tactic's slots; an empty taker
// is the player nearest the ball, empty lists leave the choice to the match.
struct SetPiecePlan {
  std::optional<std::size_t> taker;
  SetPieceDelivery delivery = SetPieceDelivery::kShort;
  std::vector<std::size_t> runners;
  std::vector<std::size_t> markers;

  friend bool operator==(const SetPiecePlan&, const SetPiecePlan&) = default;
};

// A plan for every kind, indexed by SetPieceKind.
struct SetPiecePlans {
  SetPiecePlan kickoff;
  SetPiecePlan throwIn;
  SetPiecePlan corner{
      .taker = std::nullopt, .delivery = SetPieceDelivery::kFarPost, .runners = {}, .markers = {}};
  SetPiecePlan goalKick;

  [[nodiscard]] const SetPiecePlan& of(SetPieceKind kind) const noexcept;
  [[nodiscard]] SetPiecePlan& of(SetPieceKind kind) noexcept;

  friend bool operator==(const SetPiecePlans&, const SetPiecePlans&) = default;
};

inline constexpr std::array<SetPieceKind, kSetPieceKindCount> kAllSetPieceKinds{
    SetPieceKind::kKickoff, SetPieceKind::kThrowIn, SetPieceKind::kCorner, SetPieceKind::kGoalKick};

}  // namespace ElyverseFootball::SimTactics
