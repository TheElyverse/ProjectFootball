#pragma once

#include <cstddef>
#include <optional>
#include <string_view>

#include "matchSimulation.hpp"
#include "matchState.hpp"
#include "perception.hpp"
#include "simTime.hpp"
#include "tacticalPhase.hpp"
#include "tacticalState.hpp"
#include "vec2.hpp"

namespace ElyverseFootball::SimMatch {

// How players of a side with a tactic choose where to stand; see
// docs/desired-region.md. Part of MatchConfig and therefore of every replay.
struct PositioningConfig {
  // 6 ticks at 30 Hz: players re-evaluate their position five times a
  // second.
  int intervalTicks = 6;
  // Candidates lie on two rings around the tactical target, this far apart:
  // 3 and 6 m by default, eight directions each.
  double candidateSpacing = 3.0;  // m
  // The distance from the target that counts as one unit of targetDistance.
  double targetDistanceScale = 10.0;  // m
  // Teammates closer than this crowd a position.
  double spacingRadius = 6.0;  // m
  // Opponents closer than this press on a position.
  double pressureRadius = 8.0;  // m
  // Teammates and opponents remembered with less confidence are ignored.
  double minConfidence = 0.3;
  // The player's current region keeps this much cost advantage, so he only
  // moves to a clearly better one: hysteresis against oscillating between
  // similar positions.
  double hysteresisCost = 0.15;
  // The region's centre moves at most this far per evaluation: smoothing.
  double maxShiftMeters = 3.0;  // m

  friend bool operator==(const PositioningConfig&, const PositioningConfig&) = default;
};

// Where the tactic wants the player at this index in the phase: his slot's
// place in the base shape, scaled into the phase's block -- its line height,
// length and width -- shifted with the ball, then pulled toward the lanes
// and depths his responsibilities ask for (docs/desired-region.md). Always
// on the pitch. Throws std::invalid_argument for a player of a scripted side.
[[nodiscard]] SimCore::Vec2 tacticalTarget(const MatchState& state, std::size_t playerIndex,
                                           SimTactics::TacticalPhase phase);

// What standing at candidate would cost the player at this index, with
// target his tactical target, from his own memory of teammates and
// opponents and his team's pitch control. The tactic's positioning weights
// make the total. Throws std::invalid_argument for a player of a scripted
// side.
[[nodiscard]] PositionCost evaluatePosition(const MatchState& state, std::size_t playerIndex,
                                            SimCore::Vec2 candidate, SimCore::Vec2 target,
                                            SimCore::SimTick now, double secondsPerTick,
                                            const PositioningConfig& config,
                                            const PerceptionConfig& perception);

// The player's desired region now: the cheapest of a bounded set of
// candidates -- the tactical target, two rings of eight around it, and his
// current centre with its hysteresis advantage -- with the centre moved
// toward it by at most maxShiftMeters. Deterministic, and costs at most 18
// evaluations.
[[nodiscard]] DesiredRegion chooseDesiredRegion(const MatchState& state, std::size_t playerIndex,
                                                SimTactics::TacticalPhase phase,
                                                SimCore::SimTick now, double secondsPerTick,
                                                const PositioningConfig& config,
                                                const PerceptionConfig& perception);

inline constexpr std::string_view kTacticalMovementSystemName = "tactical movement";

// Every config.intervalTicks ticks, for every side with a tactic and a
// phase: each player but the one on the ball and the side's chaser gets a
// new desired region, and its centre as his movement target. Writes
// movement targets and tactical states only. Throws std::invalid_argument
// for an interval below one tick or a distance, confidence or cost outside
// its range.
[[nodiscard]] MatchSystem makeTacticalMovementSystem(const PositioningConfig& config,
                                                     const PerceptionConfig& perception);

}  // namespace ElyverseFootball::SimMatch
