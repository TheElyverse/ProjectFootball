#pragma once

#include <string_view>

#include "ballPhysics.hpp"
#include "matchClock.hpp"
#include "matchEvents.hpp"
#include "matchSimulation.hpp"
#include "matchState.hpp"

namespace ElyverseFootball::SimMatch {

// The foul rules (docs/fouls.md). Opt-in, like restarts: matches without them
// play on after a foul as before. Part of MatchConfig and of every replay.
struct FoulConfig {
  bool enabled = false;
  // A tackle from behind: from where the victim's facing points away from,
  // the dot of his facing and the way to the tackler below this.
  double fromBehindDot = -0.5;
  // An obvious goal-scoring opportunity: the victim no farther than this from
  // the goal he attacks, and no outfield opponent but the offender nearer its
  // goal line than he is.
  double denyingDistance = 25.0;  // m
  // Advantage: the victim's side keeps the ball in the third it attacks.
  double advantageDepth = 2.0 / 3.0;

  friend bool operator==(const FoulConfig&, const FoulConfig&) = default;
};

// Throws std::invalid_argument for a fromBehindDot outside [-1, 1], a
// denyingDistance that is not positive and finite, or an advantageDepth
// outside [0, 1].
void validate(const FoulConfig& config);

// What the referee makes of a foul (docs/fouls.md#the-decision).
struct FoulJudgement {
  FoulRestart restart = FoulRestart::kFreeKick;
  Card card = Card::kNone;
  bool fromBehind = false;
  bool denyingGoal = false;

  friend bool operator==(const FoulJudgement&, const FoulJudgement&) = default;
};

// The judgement of a foul from the state it was committed in: a red card for
// denying an obvious goal-scoring opportunity outside the offender's penalty
// area; a yellow for one inside it or for a tackle from behind, a second
// yellow for a player already booked. A penalty inside the offender's penalty
// area; advantage if the victim's side has the ball at least advantageDepth up
// the pitch and nobody is sent off; a direct free kick otherwise.
[[nodiscard]] FoulJudgement judgeFoul(const MatchState& state, const FoulRecord& foul,
                                      const FoulConfig& config);

// Where a player sent off stands: off the pitch, beside the halfway line.
[[nodiscard]] SimCore::Vec2 benchPosition(const Pitch& pitch) noexcept;

inline constexpr std::string_view kFoulSystemName = "fouls";

// What the foul system is configured with: the default configuration for
// whatever a caller leaves out.
struct FoulRules {
  FoulConfig fouls{};
  BallPhysics physics{};
  MatchClockConfig clock{};
};

// Every tick (docs/fouls.md): a foul a duel recorded in this step is judged
// with judgeFoul() from the state before the step and recorded as FoulCalled.
// Its card is shown -- adding cardLostSeconds -- and a red or second yellow
// sends the offender off to benchPosition(). Unless the referee plays
// advantage, play stops: awardFreeKick() gives the victim's side a direct free
// kick where the foul was, or a penalty from the penalty spot, and ends the
// play the offside rule watched. Throws std::invalid_argument for an invalid
// configuration.
[[nodiscard]] MatchSystem makeFoulSystem(const FoulRules& rules = {});

}  // namespace ElyverseFootball::SimMatch
