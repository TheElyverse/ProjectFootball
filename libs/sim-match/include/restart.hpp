#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include "ballPhysics.hpp"
#include "matchClock.hpp"
#include "matchSimulation.hpp"
#include "matchState.hpp"
#include "pitch.hpp"
#include "restartKind.hpp"
#include "setPieces.hpp"
#include "vec2.hpp"

namespace ElyverseFootball::SimMatch {

// Whether the ball is out of play: free, at rest on the ground, and on a
// touchline or goal line, where a ball that leaves the pitch stops
// (docs/ball.md#ball-movement). A ball still in the air, even one directly over
// the line, is in play.
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
// it but no wider than the goal area (docs/match-engine.md#restarts).
[[nodiscard]] SimCore::Vec2 goalKickSpot(const Pitch& pitch, SimCore::Vec2 out) noexcept;

// Where everybody stands for a kickoff, and who takes it.
struct KickoffLineUp {
  // Parallel to MatchState::players().
  std::vector<SimCore::Vec2> positions;
  // Index in MatchState::players() of the player on the ball.
  std::size_t takerIndex = 0;

  friend bool operator==(const KickoffLineUp&, const KickoffLineUp&) = default;
};

// The line-up for a kickoff by this side (docs/match-engine.md#restarts), empty
// if it has no player. Every player stands in his side's starting formation in
// his own half: his slot of the tactic's base shape at half its depth, or for a
// scripted side, which has no formation, where he is, moved straight back to
// the halfway line if he is beyond it. An opponent inside the centre circle
// steps straight back onto it. The taker is the one the side's kickoff plan
// names if he is on the pitch (plannedTaker()), otherwise the kicking side's
// player nearest to the centre spot in that formation, ties to the lower
// player index; he
// stands carryDistance behind the spot, so the ball at his feet lies on it, or
// on his goal line if the pitch is shorter than that.
[[nodiscard]] std::optional<KickoffLineUp> lineUpForKickoff(const MatchState& state,
                                                            TeamSide kicking,
                                                            const BallPhysics& ball);

// The unit vector a side faces at a kickoff: toward the goal it attacks.
[[nodiscard]] SimCore::Vec2 kickoffFacing(TeamSide side) noexcept;

// A set piece to set up -- a throw-in, corner, goal kick, free kick or penalty:
// its restart kind, the side it is for, the taker chosen without a plan, and
// where the ball stands.
struct SetPieceAward {
  RestartKind kind = RestartKind::kThrowIn;
  TeamSide side = TeamSide::kHome;
  std::size_t fallback = 0;
  SimCore::Vec2 spot;
};

// Stops play for a set piece (docs/set-pieces.md): records RestartTaken with
// the taker its plan names -- plannedTaker(), the fallback otherwise --, and
// PossessionChanged if the ball was someone's; the ball lies dead at the spot,
// every pending action and both chasers are cleared, the offside watch and
// exemption end, its
// lostSeconds() go to the clock, and the state holds the SetPiece for the set
// piece system.
void awardSetPiece(const MatchStepContext& context, const MatchState& current,
                   MatchStateWriter& next, const SetPieceAward& award,
                   const MatchClockConfig& clock);

// A free kick or a penalty: its kind, the side it is for and where the ball
// stands.
struct FreeKick {
  RestartKind kind = RestartKind::kIndirectFreeKick;
  TeamSide side = TeamSide::kHome;
  SimCore::Vec2 spot;
};

// A free kick or penalty for its side at its spot (docs/match-engine.md#offside,
// docs/fouls.md), set up and taken as a set piece (docs/set-pieces.md): resolves
// a shot still open as kBlocked with ShotResolved, then awardSetPiece() with the
// side's player on the pitch nearest the spot, ties to the lower index, as the
// fallback taker. False, and nothing changed, if the side has no player.
bool awardFreeKick(const MatchStepContext& context, const MatchState& current,
                   MatchStateWriter& next, const FreeKick& kick, const MatchClockConfig& clock);

// Kicks off from this line-up: the ball at rest at the taker's feet, his last
// touch, PossessionChanged if he did not have it, takeKickoffLineUp(), and with
// asSetPiece a kickoff SetPiece for his side the set piece system takes
// (docs/set-pieces.md).
void kickOff(const MatchStepContext& context, const MatchState& current, MatchStateWriter& next,
             const KickoffLineUp& lineUp, bool asSetPiece);

// Puts both sides into the line-up, at rest and facing the goal they attack,
// the ball on the centre spot, and starts them as at the beginning of a match:
// without movement targets, pending actions, memories, tactical states,
// presses, chasers, pitch control, a play the offside rule watches or a set
// piece being set up. Who owns the ball is the caller's.
void takeKickoffLineUp(const MatchState& current, const KickoffLineUp& lineUp,
                       MatchStateWriter& next);

inline constexpr std::string_view kRestartSystemName = "restart";

// What the restart system is configured with: the default configuration for
// whatever a caller leaves out.
struct RestartRules {
  RestartConfig restarts{};
  BallPhysics physics{};
  MatchClockConfig clock{};
};

// Every tick while enabled (docs/match-engine.md#restarts), unless a set piece
// is being set up: if a goalkeeper has held the ball in his hands longer than
// maxHoldSeconds since he took it -- his last touch -- and has no pending pass
// or shot that lets go of it in this step, awardSetPiece() gives the other side
// a corner at the corner of his goal line nearer the ball, its player nearest
// that corner, ties to the lower index, the fallback taker. Otherwise, if the
// ball is out of play, awardSetPiece() of the throw-in, corner or goal kick
// planRestart() names, its player the fallback taker, the ball where it left
// the pitch or on goalKickSpot(); for a kickoff RestartTaken at the centre
// spot, its lostSeconds() and kickOff() as a set piece from
// lineUpForKickoff(). Disabled, it does nothing. Throws std::invalid_argument
// for a negative or non-finite maxHoldSeconds. The ball physics place the
// kickoff's taker.
[[nodiscard]] MatchSystem makeRestartSystem(const RestartRules& rules = {});

}  // namespace ElyverseFootball::SimMatch
