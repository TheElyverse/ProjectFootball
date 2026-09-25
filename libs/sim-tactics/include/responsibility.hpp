#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace ElyverseFootball::SimTactics {

// One atomic duty of a player in a tactic (docs/tactics.md). Roles such as
// "winger" are presets that combine these; the match only ever reads the
// atomic responsibilities, so a new role needs no new code.
enum class Responsibility : std::uint8_t {
  // Stay between the ball and the own goal, near the goal line.
  kGuardGoal,
  // Form the last outfield line and move with it.
  kHoldDefensiveLine,
  // Stay behind the ball when the team attacks, against a counterattack.
  kHoldRestDefence,
  // Stretch the opponent: hold the wing lane near the touchline.
  kProvideWidth,
  // Stand in the halfspace, the lane between wing and centre.
  kOccupyHalfspace,
  // Offer the ball carrier a short, open passing option.
  kSupportCarrier,
  // Attack the space behind the opponent's defensive line.
  kRunInBehind,
  // Stay close to an opponent in the own zone.
  kMarkOpponent,
  // Protect the space behind a teammate who steps out.
  kCover,
  // Join the press and close the carrier's passing lanes.
  kClosePressingLine,
};

inline constexpr std::size_t kResponsibilityCount = 10;

inline constexpr std::array<Responsibility, kResponsibilityCount> kAllResponsibilities{
    Responsibility::kGuardGoal,       Responsibility::kHoldDefensiveLine,
    Responsibility::kHoldRestDefence, Responsibility::kProvideWidth,
    Responsibility::kOccupyHalfspace, Responsibility::kSupportCarrier,
    Responsibility::kRunInBehind,     Responsibility::kMarkOpponent,
    Responsibility::kCover,           Responsibility::kClosePressingLine,
};

// "guardGoal", "holdDefensiveLine", ... as tactic files spell them; "unknown"
// for a value outside the enumerators.
[[nodiscard]] std::string_view responsibilityName(Responsibility responsibility) noexcept;

// The responsibility a name from responsibilityName() stands for; empty for
// any other text.
[[nodiscard]] std::optional<Responsibility> parseResponsibility(std::string_view name) noexcept;

// True only for the declared enumerators.
[[nodiscard]] bool isKnown(Responsibility responsibility) noexcept;

// Two duties that pull the same player to different places, so one slot must
// not hold both: width and halfspace, running in behind and staying behind
// the ball (as rest defence or in the defensive line). The goalkeeper's duty
// excludes every other one.
[[nodiscard]] bool contradicts(Responsibility first, Responsibility second) noexcept;

// A responsibility and how much it matters to the slot, in (0, 1]. The weight
// scales its pull on the player's decisions; 1 is a primary duty.
struct SlotResponsibility {
  Responsibility responsibility = Responsibility::kSupportCarrier;
  double weight = 1.0;

  friend bool operator==(const SlotResponsibility&, const SlotResponsibility&) = default;
};

// Situations that tell a team without the ball to start pressing (GDD
// section 8.7). A tactic selects the triggers its players react to.
enum class PressingTrigger : std::uint8_t {
  // The carrier's first touch left the ball away from him.
  kPoorFirstTouch,
  // The ball was just played back toward the carrier's own goal.
  kBackPass,
  // The carrier received facing his own goal.
  kReceiverFacingOwnGoal,
  // No teammate of the carrier is close enough to help.
  kIsolatedReceiver,
  // A pass rolls slowly enough to be reached before or as it arrives.
  kSlowPass,
};

inline constexpr std::size_t kPressingTriggerCount = 5;

inline constexpr std::array<PressingTrigger, kPressingTriggerCount> kAllPressingTriggers{
    PressingTrigger::kPoorFirstTouch,
    PressingTrigger::kBackPass,
    PressingTrigger::kReceiverFacingOwnGoal,
    PressingTrigger::kIsolatedReceiver,
    PressingTrigger::kSlowPass,
};

// "poorFirstTouch", "backPass", ... ; "unknown" outside the enumerators.
[[nodiscard]] std::string_view pressingTriggerName(PressingTrigger trigger) noexcept;

[[nodiscard]] std::optional<PressingTrigger> parsePressingTrigger(std::string_view name) noexcept;

[[nodiscard]] bool isKnown(PressingTrigger trigger) noexcept;

}  // namespace ElyverseFootball::SimTactics
