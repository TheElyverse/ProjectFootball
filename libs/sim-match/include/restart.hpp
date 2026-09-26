#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

#include "ballMovement.hpp"
#include "matchSimulation.hpp"
#include "matchState.hpp"
#include "restartKind.hpp"

namespace ElyverseFootball::SimMatch {

// Whether the ball is out of play: free, at rest, and on a touchline or goal
// line, where a ball that leaves the pitch stops (docs/ball-movement.md).
[[nodiscard]] bool isOutOfPlay(const BallState& ball, const Pitch& pitch) noexcept;
[[nodiscard]] bool isOutOfPlay(const MatchState& state) noexcept;

// Who restarts play and how.
struct RestartPlan {
  RestartKind kind = RestartKind::kThrowIn;
  // Index in MatchState::players() of the player who gets the ball.
  std::size_t playerIndex = 0;

  friend bool operator==(const RestartPlan&, const RestartPlan&) = default;
};

// The restart for a ball out of play, empty while it is in play or when the
// side due to restart has no player. Over a touchline: a throw-in for the
// side that did not touch the ball last. Over a goal line: a corner if the
// side defending that line touched it last, a goal kick otherwise. The ball
// goes to the side's player nearest to it -- for a goal kick, its goalkeeper
// if it has one. Ties go to the lower player index; no random number is
// drawn.
[[nodiscard]] std::optional<RestartPlan> planRestart(const MatchState& state);

inline constexpr std::string_view kRestartSystemName = "restart";

// Every tick while enabled: if the ball is out of play, gives it to the
// player planRestart() names, at his feet, and records RestartTaken and
// PossessionChanged. The player is not moved: a restart only settles who
// plays on. Disabled, it does nothing.
// ball places the ball at the taker's feet.
[[nodiscard]] MatchSystem makeRestartSystem(const RestartConfig& config, const BallPhysics& ball);

}  // namespace ElyverseFootball::SimMatch
