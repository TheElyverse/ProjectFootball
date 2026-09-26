#pragma once

#include <optional>
#include <string_view>

#include "matchSimulation.hpp"
#include "matchState.hpp"
#include "pitchControlGrid.hpp"

namespace ElyverseFootball::SimMatch {

// How the pitch-control grid is built and refreshed; see
// docs/pitch-control.md. Part of MatchConfig and therefore of every replay.
struct PitchControlConfig {
  // 10 ticks at 30 Hz: three refreshes a second, like the tactical phases.
  int intervalTicks = 10;
  // The side of a square cell. 2 m gives a 30 x 20 grid on the 60 x 40 m
  // sandbox pitch.
  double cellSize = 2.0;  // m
  // How sharply control changes with the arrival advantage: a team this
  // many seconds earlier than the other controls about 73 % of a cell.
  double controlSeconds = 0.5;

  friend bool operator==(const PitchControlConfig&, const PitchControlConfig&) = default;
};

// The smallest cell a configuration may ask for. A refresh estimates one
// arrival time per player and cell, so a tiny cell size read from a replay
// file must not make a step arbitrarily slow: half a meter is 9600 cells on
// the sandbox pitch, sixteen times the default.
inline constexpr double kMinPitchControlCellSize = 0.5;  // m

// The grid for a state: every cell's centre, and for each side the earliest
// time any of its players reaches it by estimateArrivalSeconds() -- the
// movement model, from where each player is and how he moves. Cells whose
// centre lies past the pitch edge use the nearest point on the pitch.
// Deterministic: the same state and configuration give an equal grid.
//
// If reuse holds a grid of the same columns and rows, its arrival-time
// storage is recycled instead of allocating a fresh one -- the common case,
// since a match's pitch and cell size do not change between refreshes.
//
// Throws std::invalid_argument if the pitch and cell size would need more
// cells than the grid can affordably hold.
[[nodiscard]] PitchControlGrid computePitchControl(const MatchState& state,
                                                   const PitchControlConfig& config,
                                                   std::optional<PitchControlGrid> reuse = std::nullopt);

inline constexpr std::string_view kPitchControlSystemName = "pitch control";

// Refreshes the grid cached in the state every config.intervalTicks ticks.
// Writes the pitch-control grid only. Throws std::invalid_argument for an
// interval below one tick, a cell size below kMinPitchControlCellSize or not
// finite, or a controlSeconds that is not positive and finite.
[[nodiscard]] MatchSystem makePitchControlSystem(const PitchControlConfig& config);

}  // namespace ElyverseFootball::SimMatch
