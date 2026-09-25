#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

#include "matchState.hpp"
#include "pitch.hpp"
#include "vec2.hpp"

namespace ElyverseFootball::SimMatch {

// Semantic zones of the pitch (docs/zones.md): the words tactics use --
// wing, halfspace, between the lines -- as derived data. Zones are queries
// over the pitch and the current state; no simulation rule treats a zone
// boundary as a hard limit, so a player a centimetre across one is not in
// a different game.

// The five vertical lanes, from a side's point of view: left and right as
// seen by a player attacking the opponent's goal.
enum class Lane : std::uint8_t {
  kLeftWing,
  kLeftHalfspace,
  kCentre,
  kRightHalfspace,
  kRightWing,
};

// "leftWing", "leftHalfspace", "centre", ...; "unknown" outside the
// enumerators.
[[nodiscard]] std::string_view laneName(Lane lane) noexcept;

// Lane boundaries as fractions of the pitch width from the side's left
// touchline: wings are the outer fifths, halfspaces the next fifths inward,
// the centre the middle fifth.
inline constexpr double kLaneFraction = 0.2;

// The lane a position lies in for this side. Positions off the pitch belong
// to the nearest lane.
[[nodiscard]] Lane laneOf(TeamSide side, SimCore::Vec2 position, const Pitch& pitch) noexcept;

// The lane as a rectangle over the whole pitch length.
[[nodiscard]] PitchRect laneRect(TeamSide side, Lane lane, const Pitch& pitch) noexcept;

// The pitch y of a lane's centre line.
[[nodiscard]] double laneCenterY(TeamSide side, Lane lane, const Pitch& pitch) noexcept;

// The three horizontal thirds from a side's point of view.
enum class Third : std::uint8_t {
  kDefensive,
  kMiddle,
  kAttacking,
};

[[nodiscard]] std::string_view thirdName(Third third) noexcept;

// The third a position lies in for this side, by its depth.
[[nodiscard]] Third thirdOf(TeamSide side, SimCore::Vec2 position, const Pitch& pitch) noexcept;

// The third as a rectangle over the whole pitch width.
[[nodiscard]] PitchRect thirdRect(TeamSide side, Third third, const Pitch& pitch) noexcept;

// A team's shape as its outfield players form it right now: depths from the
// team's own goal line, in meters. The goalkeeper -- the player whose slot
// guards the goal in the team's tactic -- is not part of it; a scripted side
// has no goalkeeper, so all its players are.
struct TeamShape {
  // The deepest outfield player: the last line, behind which there is space.
  double defensiveLine = 0.0;
  // The median outfield depth.
  double midfieldLine = 0.0;
  // The highest outfield player.
  double frontLine = 0.0;
  // From the defensive to the front line: the vertical compactness.
  double length = 0.0;
  // Between the outermost outfield players, in meters across the pitch.
  double width = 0.0;
  // The pitch y of those two players.
  double minY = 0.0;
  double maxY = 0.0;
  // The mean outfield position, in pitch coordinates.
  SimCore::Vec2 centroid;

  friend bool operator==(const TeamShape&, const TeamShape&) = default;
};

// The side's shape in the state. Empty for a side without outfield players.
[[nodiscard]] std::optional<TeamShape> measureTeamShape(const MatchState& state, TeamSide side);

// Whether the player at this index is his side's goalkeeper: his slot holds
// guardGoal in his side's tactic.
[[nodiscard]] bool isGoalkeeper(const MatchState& state, std::size_t playerIndex);

// The space between the opponent's lines: from his defensive line to his
// midfield line, across the width his outfield players span. Where a side
// attacking him finds room to receive between the lines.
[[nodiscard]] PitchRect betweenLines(TeamSide opponent, const TeamShape& opponentShape,
                                     const Pitch& pitch) noexcept;

// The space behind the opponent's defensive line, up to his goal line, over
// the whole width: where runs in behind go.
[[nodiscard]] PitchRect behindLine(TeamSide opponent, const TeamShape& opponentShape,
                                   const Pitch& pitch) noexcept;

// How far behind the ball a side's rest defence stands: from
// kRestDefenceNear to kRestDefenceFar meters closer to its own goal.
inline constexpr double kRestDefenceNear = 5.0;  // m
inline constexpr double kRestDefenceFar = 20.0;  // m

// Where a side in possession keeps players against a counterattack: behind
// the ball by kRestDefenceNear to kRestDefenceFar meters, across the centre
// and both halfspaces, never past the own goal line.
[[nodiscard]] PitchRect restDefenceZone(TeamSide side, SimCore::Vec2 ballPosition,
                                        const Pitch& pitch) noexcept;

}  // namespace ElyverseFootball::SimMatch
