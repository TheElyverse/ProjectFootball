#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include "ballMovement.hpp"
#include "matchSimulation.hpp"
#include "matchState.hpp"
#include "restartKind.hpp"
#include "vec2.hpp"

namespace ElyverseFootball::SimMatch {

// Whether the ball is out of play: free, at rest on the ground, and on a
// touchline or goal line, where a ball that leaves the pitch stops
// (docs/ball-movement.md). A ball still in the air, even one directly over the
// line, is in play.
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
// side due to restart has no player. After a goal -- the state's last goal,
// if nobody has touched the ball since: a kickoff for the side that conceded
// it, by the taker of its line-up (lineUpForKickoff()). Over a touchline: a throw-in
// for the side that did not touch the ball last. Over a goal line: a corner if the
// side defending that line touched it last, a goal kick otherwise. The ball
// goes to the side's player nearest to it -- for a goal kick, its goalkeeper
// if it has one. Ties go to the lower player index; no random number is
// drawn.
[[nodiscard]] std::optional<RestartPlan> planRestart(const MatchState& state);

// Where a goal kick is taken from: on the front edge of the goal area at the
// goal line the ball left the pitch over at `out`, level with where it left
// it but no wider than the goal area (docs/restarts.md).
[[nodiscard]] SimCore::Vec2 goalKickSpot(const Pitch& pitch, SimCore::Vec2 out) noexcept;

// Where everybody stands for a kickoff, and who takes it.
struct KickoffLineUp {
  // Parallel to MatchState::players().
  std::vector<SimCore::Vec2> positions;
  // Index in MatchState::players() of the player on the ball.
  std::size_t takerIndex = 0;

  friend bool operator==(const KickoffLineUp&, const KickoffLineUp&) = default;
};

// The line-up for a kickoff by this side (docs/restarts.md), empty if it has
// no player. Every player stands in his side's starting formation in his own
// half: his slot of the tactic's base shape at half its depth, or for a
// scripted side, which has no formation, where he is, moved straight back to
// the halfway line if he is beyond it. An opponent inside the centre circle
// steps straight back onto it. The taker is the kicking side's player nearest
// to the centre spot in that formation, ties to the lower player index; he
// stands carryDistance behind the spot, so the ball at his feet lies on it,
// or on his goal line if the pitch is shorter than that.
[[nodiscard]] std::optional<KickoffLineUp> lineUpForKickoff(const MatchState& state,
                                                            TeamSide kicking,
                                                            const BallPhysics& ball);

// The unit vector a side faces at a kickoff: toward the goal it attacks.
[[nodiscard]] SimCore::Vec2 kickoffFacing(TeamSide side) noexcept;

inline constexpr std::string_view kRestartSystemName = "restart";

// Every tick while enabled: if the ball is out of play, gives it to the
// player planRestart() names, at his feet, and records RestartTaken -- at the
// centre spot for a kickoff, otherwise where the ball left the pitch -- and
// PossessionChanged. For a throw-in and a corner nobody is moved: the restart
// only settles who plays on. For a goal kick the taker stands behind the
// ball on goalKickSpot(), at rest and facing the goal he attacks, the ball at
// his feet. For a kickoff both sides are
// placed in lineUpForKickoff()'s positions, at rest and facing the goal they
// attack, with the ball on the centre spot, and start as at the beginning of
// a match: without movement targets, pending actions, memories, tactical
// states, presses or chasers. Disabled, it does nothing.
// ball places the ball at the taker's feet.
[[nodiscard]] MatchSystem makeRestartSystem(const RestartConfig& config, const BallPhysics& ball);

}  // namespace ElyverseFootball::SimMatch
