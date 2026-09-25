#pragma once

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include "ids.hpp"
#include "responsibility.hpp"
#include "simTime.hpp"

namespace ElyverseFootball::SimMatch {

// The part a player plays in a coordinated press (docs/pressing.md).
enum class PressRole : std::uint8_t {
  // Close the carrier down on the line to his nearest option.
  kPress,
  // Stand in the lane to one of the carrier's options.
  kBlockLane,
  // Protect the space behind the presser.
  kCover,
};

// "press", "blockLane", "cover"; "unknown" outside the enumerators.
[[nodiscard]] std::string_view pressRoleName(PressRole role) noexcept;

// One player's role in a press, and whom it is about: the carrier for the
// presser, the option whose lane he blocks, the presser he covers.
struct PressAssignment {
  SimCore::PlayerId player;
  PressRole role = PressRole::kPress;
  SimCore::PlayerId subject;

  friend bool operator==(const PressAssignment&, const PressAssignment&) = default;
};

// A side's press in progress: whom it presses, since when, what started it --
// a trigger, or empty for a press of the pressing phase -- and who plays which
// role.
struct TeamPress {
  SimCore::PlayerId carrier;
  SimCore::SimTick since;
  std::optional<SimTactics::PressingTrigger> trigger;
  std::vector<PressAssignment> assignments;

  friend bool operator==(const TeamPress&, const TeamPress&) = default;
};

// How a press ended.
enum class PressOutcome : std::uint8_t {
  // The pressing side won the ball.
  kBallRegained,
  // The carrier's side got the ball to another player: played out of
  // pressure.
  kPassedOut,
  // The press ran out of time without either: the carrier escaped it.
  kCarrierEscaped,
};

// "ballRegained", "passedOut", "carrierEscaped"; "unknown" outside the
// enumerators.
[[nodiscard]] std::string_view pressOutcomeName(PressOutcome outcome) noexcept;

}  // namespace ElyverseFootball::SimMatch
