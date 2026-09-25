#pragma once

#include <optional>
#include <string_view>

#include "ids.hpp"
#include "tacticalState.hpp"
#include "vec2.hpp"

namespace ElyverseFootball::SimMatch {

// The weighted parts of an action's utility, one number per consideration,
// each already multiplied by its weight: their sum is the utility. Kept apart
// so a decision can be explained (docs/off-ball-movement.md). A
// consideration that does not apply to an action is 0.
struct ActionScores {
  // How well the action matches the player's responsibilities.
  double responsibility = 0.0;
  // How much worse the target is than his desired region, as positioning
  // cost: 0 or negative.
  double region = 0.0;
  // How much of the target his team controls.
  double space = 0.0;
  // How open the passing lane from the carrier to the target is.
  double lane = 0.0;
  // How strongly the tactic or the situation urges the action.
  double urgency = 0.0;
  // The run it takes to get there: 0 or negative.
  double effort = 0.0;

  [[nodiscard]] double total() const noexcept {
    return responsibility + region + space + lane + urgency + effort;
  }

  friend bool operator==(const ActionScores&, const ActionScores&) = default;
};

// The consideration that contributed most to a utility, by absolute value:
// "responsibility", "region", "space", "lane", "urgency" or "effort". Ties go
// to the one listed first.
[[nodiscard]] std::string_view dominantScore(const ActionScores& scores) noexcept;

// One option of a player without the ball, with everything that went into
// its utility.
struct ActionCandidate {
  ActionType type = ActionType::kHoldPosition;
  SimCore::Vec2 target;
  // Whom the action is about: the carrier to support, an opponent to mark.
  std::optional<SimCore::PlayerId> subject;
  ActionScores scores;
  double utility = 0.0;

  friend bool operator==(const ActionCandidate&, const ActionCandidate&) = default;
};

}  // namespace ElyverseFootball::SimMatch
