#pragma once

#include <cstddef>
#include <string_view>
#include <vector>

#include "matchSimulation.hpp"
#include "matchState.hpp"
#include "observation.hpp"
#include "simTime.hpp"
#include "vec2.hpp"

namespace ElyverseFootball::SimMatch {

// How players perceive the match; see docs/perception.md. Part of MatchConfig
// and therefore of every replay.
struct PerceptionConfig {
  // 3 ticks at 30 Hz: perception updates ten times a second.
  int intervalTicks = 3;
  // Nothing farther away is seen, in front or not.
  double viewDistance = 60.0;  // m
  // The whole width of the vision cone, centered on the facing.
  double fieldOfViewDegrees = 180.0;
  // Closer than this, a player senses an entity in every direction.
  double awarenessRadius = 3.0;  // m
  // An entity unseen for this long is forgotten; confidence falls linearly
  // from 1 to 0 over it.
  double memorySeconds = 3.0;
  // How far estimatePosition() projects an unseen entity along its last
  // seen velocity.
  double extrapolationSeconds = 1.0;

  friend bool operator==(const PerceptionConfig&, const PerceptionConfig&) = default;
};

// Whether a player standing where observer stands, looking along his facing,
// sees a position: within viewDistance, and either inside the vision cone or
// within awarenessRadius. Both bounds are inclusive. There is no occlusion:
// players do not hide one another yet. The cone's cosine is computed from
// basic arithmetic, so the answer is the same on every platform.
[[nodiscard]] bool canSee(const PlayerMatchState& observer, SimCore::Vec2 position,
                          const PerceptionConfig& config) noexcept;

// Where the observed entity probably is at tick now: its last seen position,
// moved along its last seen velocity for the time since, but for at most
// extrapolationSeconds.
[[nodiscard]] SimCore::Vec2 estimatePosition(const Observation& observation, SimCore::SimTick now,
                                             double secondsPerTick,
                                             const PerceptionConfig& config) noexcept;

// A player someone remembers, where he believes that player is now.
struct RememberedPlayer {
  // Index in MatchState::players().
  std::size_t index = 0;
  SimCore::PlayerId playerId;
  // estimatePosition() of the observation.
  SimCore::Vec2 position;
  SimCore::Vec2 velocity;
  double confidence = 0.0;
  bool teammate = false;

  friend bool operator==(const RememberedPlayer&, const RememberedPlayer&) = default;
};

// Every player the player at observerIndex remembers with at least
// minConfidence, in his memory's order (by id). Squad membership -- who is a
// teammate -- is known; positions are believed.
[[nodiscard]] std::vector<RememberedPlayer> rememberedPlayers(
    const MatchState& state, std::size_t observerIndex, SimCore::SimTick now, double secondsPerTick,
    const PerceptionConfig& config, double minConfidence);

// One perception update for the player at observerIndex at tick now: every
// entity he can see gets a fresh observation with confidence 1; every entity
// he remembers but cannot see keeps its last seen values with confidence
// 1 - age / memorySeconds, and is forgotten once that age reaches
// memorySeconds. Writes the result, ordered by entity, into memory.
void perceive(const MatchState& state, std::size_t observerIndex, SimCore::SimTick now,
              double secondsPerTick, const PerceptionConfig& config, PlayerPerception& memory);

inline constexpr std::string_view kPerceptionSystemName = "perception";

// Updates every player's memory with perceive() every config.intervalTicks
// ticks. Writes perception memories only. Throws std::invalid_argument for an
// interval below one tick, a field of view outside (0, 360] degrees, a
// negative distance or extrapolation, or a memory that is not positive; every
// value must be finite.
[[nodiscard]] MatchSystem makePerceptionSystem(const PerceptionConfig& config);

}  // namespace ElyverseFootball::SimMatch
