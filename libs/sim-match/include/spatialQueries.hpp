#pragma once

#include <cstddef>
#include <optional>
#include <vector>

#include "ids.hpp"
#include "matchState.hpp"
#include "pitch.hpp"
#include "vec2.hpp"

namespace ElyverseFootball::SimMatch {

// Which players a query considers: everyone by default, or one side, and
// optionally not one particular player -- typically the one asking.
//
//   PlayerFilter{}                                    everyone
//   PlayerFilter::onSide(TeamSide::kAway)             the away side
//   PlayerFilter::onSide(side).except(playerId)       teammates of playerId
struct PlayerFilter {
  std::optional<TeamSide> side;
  std::optional<SimCore::PlayerId> excluded;

  [[nodiscard]] static PlayerFilter onSide(const TeamSide teamSide) noexcept {
    return {.side = teamSide, .excluded = std::nullopt};
  }

  [[nodiscard]] PlayerFilter except(const SimCore::PlayerId playerId) const noexcept {
    return {.side = side, .excluded = playerId};
  }

  [[nodiscard]] bool accepts(const PlayerMatchState& player) const noexcept {
    return (!side || player.side == *side) && (!excluded || player.playerId != *excluded);
  }
};

struct NearbyPlayer {
  // Index in MatchState::players().
  std::size_t index = 0;
  SimCore::PlayerId playerId;
  double distance = 0.0;

  friend bool operator==(const NearbyPlayer&, const NearbyPlayer&) = default;
};

// Every player the filter accepts within radius meters of center, nearest
// first. The radius is inclusive. Players at the same distance are ordered by
// id, so the result is the same whatever the order of players in the state --
// a system iterating over it stays deterministic. Distances are compared
// squared, so the ordering is exact.
[[nodiscard]] std::vector<NearbyPlayer> findPlayersWithin(const MatchState& state,
                                                          SimCore::Vec2 center, double radius,
                                                          const PlayerFilter& filter = {});

// The nearest player the filter accepts, at any distance, with the same
// tie-break; empty if the filter accepts no one.
[[nodiscard]] std::optional<NearbyPlayer> findNearestPlayer(const MatchState& state,
                                                            SimCore::Vec2 center,
                                                            const PlayerFilter& filter = {});

// How many seconds the player needs to reach a position at full effort,
// running through it rather than stopping on it -- what matters for reaching
// a ball. Uses the movement model of docs/player-movement.md: he accelerates
// at his acceleration from his current speed toward the position, up to his
// maximum speed. Empty if the position is not finite or off the pitch, where
// no movement target can lead.
//
// Only the part of his velocity that points at the position counts; a player
// running away starts by slowing down. Turning away from a sideways run costs
// time the estimate does not include, so it is optimistic for players whose
// velocity points elsewhere.
[[nodiscard]] std::optional<double> estimateArrivalSeconds(const PlayerMatchState& player,
                                                           SimCore::Vec2 position,
                                                           const Pitch& pitch) noexcept;

}  // namespace ElyverseFootball::SimMatch
