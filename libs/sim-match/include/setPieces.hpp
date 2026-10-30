#pragma once

#include <cstddef>
#include <optional>
#include <string_view>
#include <vector>

#include "ballPhysics.hpp"
#include "matchSimulation.hpp"
#include "matchState.hpp"
#include "passing.hpp"
#include "setPiecePlan.hpp"
#include "shotCandidates.hpp"
#include "vec2.hpp"

namespace ElyverseFootball::SimMatch {

// How set pieces are set up and taken (docs/set-pieces.md). Part of
// MatchConfig and of every replay.
struct SetPieceConfig {
  // Real seconds the players have to take up their positions before the taker
  // may play the ball, by kind.
  double throwInSeconds = 8.0;    // s
  double cornerSeconds = 20.0;    // s
  double goalKickSeconds = 15.0;  // s
  double kickoffSeconds = 0.0;    // s
  double freeKickSeconds = 20.0;  // s
  double penaltySeconds = 10.0;   // s
  // A free kick no farther than this from the goal it attacks goes for goal:
  // the defenders build a wall and the attackers go into the box.
  double freeKickRange = 32.0;  // m
  // How many players the wall has, at the free kick's distance from goal:
  // wallPlayers within 22 m, one fewer within 27 m, two fewer beyond.
  int wallPlayers = 4;
  // How far apart the wall's players stand.
  double wallSpacing = 0.6;  // m
  // How near his place behind the ball the taker must stand to play it, and
  // how near their places everyone else with one, unless they still are not
  // there after the setup time and waitSeconds more.
  double readyRadius = 0.75;  // m
  double placeRadius = 2.0;   // m
  double waitSeconds = 10.0;  // s
  // How long a lofted delivery is in the air.
  double flightSeconds = 1.6;  // s
  // How far goal side of his man a marker stands.
  double markDistance = 1.0;  // m
  // Without a plan naming them: how many players go into the box at a corner,
  // and how many come short at a throw-in or a goal kick.
  int cornerRunners = 4;
  int shortOptions = 2;
  // How far the other side keeps from the ball at a throw-in (Law 15); at a
  // corner a centre circle's radius (Law 17).
  double throwInDistance = 2.0;  // m

  friend bool operator==(const SetPieceConfig&, const SetPieceConfig&) = default;
};

// Throws std::invalid_argument for negative or non-finite seconds or
// distances, a readyRadius or flightSeconds that is not positive, or a
// negative number of players.
void validate(const SetPieceConfig& config);

// The real seconds the players have before a set piece of this kind.
[[nodiscard]] double setupSeconds(SimTactics::SetPieceKind kind,
                                  const SetPieceConfig& config) noexcept;

// The plans of a side: its tactic's, the default plans for a side without one.
[[nodiscard]] const SimTactics::SetPiecePlans& plansOf(const MatchState& state, TeamSide side);

// The player who takes a set piece of this kind for `side`: the one his plan
// names, if he is on the pitch, otherwise `fallback`.
[[nodiscard]] std::size_t plannedTaker(const MatchState& state, TeamSide side,
                                       SimTactics::SetPieceKind kind, std::size_t fallback);

// Where the taker stands to play the ball: behind it, facing where it goes.
[[nodiscard]] SimCore::Vec2 takerSpot(const MatchState& state, const SetPiece& setPiece,
                                      const BallPhysics& physics);

// Where every player goes while the set piece is set up
// (docs/set-pieces.md#positions), parallel to MatchState::players(): empty for
// a player the set piece leaves to the tactical systems.
[[nodiscard]] std::vector<std::optional<SimCore::Vec2>> setPieceTargets(
    const MatchState& state, const SetPiece& setPiece, const SetPieceConfig& config,
    const BallPhysics& physics);

// The pass the plan asks for (docs/set-pieces.md#the-delivery): at a corner
// lofted to the runner of the planned area or short to the short option, at a
// throw-in, goal kick or kickoff short to a near teammate or long and lofted
// to the side's most advanced player. Empty if the side has nobody to play to,
// and for a free kick or penalty, whose taker decides for himself.
[[nodiscard]] std::optional<PassIntent> plannedDelivery(const MatchState& state,
                                                        const SetPiece& setPiece,
                                                        const SetPieceConfig& config,
                                                        const BallPhysics& physics,
                                                        const PassConfig& passing);

inline constexpr std::string_view kSetPieceSystemName = "set pieces";

struct SetPieceRules {
  SetPieceConfig setPieces{};
  BallPhysics physics{};
  PassConfig passing{};
  // How a penalty taker weighs his shots (docs/set-pieces.md#penalties).
  ShotCandidateRules shots{};
  double temperature = 0.15;
};

// Every tick while a set piece is set up (docs/set-pieces.md): every player
// with a place in it gets setPieceTargets() as his movement target, over the
// tactical movement's. Once setupSeconds() have passed, the taker stands
// within readyRadius of takerSpot() and every other player with a place within
// placeRadius of it -- or waitSeconds more have passed --, the taker plays
// plannedDelivery(): the
// ball is his, at his feet, with the pass pending, the set piece is over, and
// SetPieceTaken and PossessionChanged are recorded; at a throw-in, a corner or
// a goal kick his touch is free of offside. A free kick's taker gets the ball with nothing pending
// and decides like any carrier; an indirect one's touch is held as
// MatchState::indirectKick() until another player's touch, which this system
// notices the step after. A penalty's taker shoots: generateShotCandidates()
// chosen by utility at `temperature` from the kAi stream
// (docs/set-pieces.md#penalties). Throws std::invalid_argument for an invalid
// configuration.
[[nodiscard]] MatchSystem makeSetPieceSystem(const SetPieceRules& rules = {});

}  // namespace ElyverseFootball::SimMatch
