#pragma once

#include <span>
#include <string_view>
#include <vector>

#include "ballPhysics.hpp"
#include "matchClock.hpp"
#include "matchSimulation.hpp"
#include "matchState.hpp"
#include "perception.hpp"
#include "pitch.hpp"
#include "vec2.hpp"

namespace ElyverseFootball::SimMatch {

// The offside rule (docs/match-engine.md#offside). Opt-in, like restarts:
// matches without it keep playing as before. Part of MatchConfig and of every
// replay.
struct OffsideConfig {
  bool enabled = false;
  // How far a player may be nearer the opponent's goal line than the offside
  // line and still be level with it: onside. The rule's tolerance, so a
  // marginal call does not hang on rounding.
  double levelMeters = 0.1;  // m

  friend bool operator==(const OffsideConfig&, const OffsideConfig&) = default;
};

// Throws std::invalid_argument for a level tolerance that is negative or not
// finite.
void validate(const OffsideConfig& config);

// The offside line for the side attacking, as a depth from its own goal line:
// the deepest of the second-last opponent, the ball and the halfway line. The
// second-last opponent is the second deepest of `opponents`; with fewer than
// two it is the opponent's goal line.
[[nodiscard]] double offsideLine(TeamSide attacking, std::span<const SimCore::Vec2> opponents,
                                 SimCore::Vec2 ball, const Pitch& pitch);

// Whether a player of `side` at `position` is in an offside position against
// this line: nearer the opponent's goal line than it by more than levelMeters.
[[nodiscard]] bool isOffsidePosition(TeamSide side, SimCore::Vec2 position, double line,
                                     const Pitch& pitch, const OffsideConfig& config) noexcept;

// The offside line as a player of `side` believes it: offsideLine() of the
// opponents he remembers and the ball where it is -- but remembering fewer
// than two, he takes the ball or the halfway line, whichever is deeper, rather
// than believe nobody offside.
[[nodiscard]] double believedOffsideLine(TeamSide side,
                                         std::span<const SimCore::Vec2> rememberedOpponents,
                                         SimCore::Vec2 ball, const Pitch& pitch);
[[nodiscard]] double believedOffsideLine(TeamSide side,
                                         const std::vector<RememberedPlayer>& remembered,
                                         SimCore::Vec2 ball, const Pitch& pitch);

inline constexpr std::string_view kOffsideSystemName = "offside";

// What the offside system is configured with: the default configuration for
// whatever a caller leaves out.
struct OffsideRules {
  OffsideConfig offside{};
  BallPhysics physics{};
  MatchClockConfig clock{};
};

// Every tick (docs/match-engine.md#offside). First a touch the step made --
// the ball's last touch changed -- on the play the state watches: by one of
// its players in an offside position it is an offence, OffsideCalled, and
// awardFreeKick() for the other side where he stands; by a teammate, or a
// deliberate one by an opponent -- anything but deflecting or parrying a shot
// -- it ends the watch. Then a pass or shot the step played starts a new watch
// with the players in an offside position at that moment, as the state stood
// before the step, unless none is or it is the first play after a throw-in, a
// goal kick or a corner (MatchState::offsideExemption()). Throws
// std::invalid_argument for an invalid configuration.
[[nodiscard]] MatchSystem makeOffsideSystem(const OffsideRules& rules = {});

}  // namespace ElyverseFootball::SimMatch
