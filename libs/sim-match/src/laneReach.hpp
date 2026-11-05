#pragma once

// Who gets to a ball on its way first, as a player on the ball imagines it:
// shared by the pass and shot candidates; internal to sim-match.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <optional>
#include <vector>

#include "ballPhysics.hpp"
#include "matchState.hpp"
#include "offside.hpp"
#include "perception.hpp"
#include "simTime.hpp"
#include "spatialQueries.hpp"
#include "vec2.hpp"

namespace ElyverseFootball::SimMatch::LaneReach {

// Spacing of the points along a lane where the players' reach is checked.
inline constexpr double kLaneStep = 0.5;  // m

// A smooth step from 1 at value <= -1 through 1/2 at 0 to 0 at value >= 1:
// the cubic smoothstep, built from arithmetic only, so no std::exp and none
// of its platform differences enter the scores.
[[nodiscard]] inline double fallingStep(const double value) noexcept {
  const double rising = std::clamp((1.0 - value) / 2.0, 0.0, 1.0);
  return rising * rising * (3.0 - (2.0 * rising));
}

// Seconds a ball kicked at `speed` needs to roll `distance` meters, from
// s = v·t − a·t²/2; infinite if it stops before.
[[nodiscard]] inline double ballSeconds(const double distance, const double speed,
                                        const BallPhysics& physics) noexcept {
  const double deceleration = physics.rollingDeceleration;
  const double remaining = (speed * speed) - (2.0 * deceleration * distance);
  if (remaining < 0.0) {
    return std::numeric_limits<double>::infinity();
  }
  return (speed - std::sqrt(remaining)) / deceleration;
}

// A player the carrier remembers, as he imagines him now: where and how fast
// he believes he is, with default movement limits -- the carrier does not
// know anyone's attributes.
struct Remembered {
  PlayerMatchState player;
  double confidence = 0.0;
  // His index in the state's players.
  std::size_t index = 0;
};

// The players the carrier remembers with at least minConfidence, split into
// his teammates and his opponents, in the order of his memory.
struct RememberedPlayers {
  std::vector<Remembered> teammates;
  std::vector<Remembered> opponents;
};

// The offside line as the player on the ball believes it from the opponents he
// remembers, with offside on; empty with it off (docs/match-engine.md#offside).
[[nodiscard]] inline std::optional<double> believedLine(const std::vector<Remembered>& opponents,
                                                        const TeamSide side,
                                                        const SimCore::Vec2 ball,
                                                        const Pitch& pitch,
                                                        const OffsideConfig& offside) {
  if (!offside.enabled) {
    return std::nullopt;
  }
  std::vector<SimCore::Vec2> positions;
  positions.reserve(opponents.size());
  for (const Remembered& opponent : opponents) {
    positions.push_back(opponent.player.position);
  }
  return believedOffsideLine(side, positions, ball, pitch);
}

[[nodiscard]] inline RememberedPlayers rememberedPlayers(const MatchState& state,
                                                         const std::size_t carrierIndex,
                                                         const SimCore::StepTime time,
                                                         const PerceptionConfig& perception,
                                                         const double minConfidence) {
  const PlayerMatchState& carrier = state.players()[carrierIndex];
  RememberedPlayers remembered;
  for (const Observation& observation : state.perception(carrierIndex).observations) {
    if (observation.entity.isBall() || observation.confidence < minConfidence) {
      continue;
    }
    const auto index = findPlayerIndex(state, observation.entity.playerId());
    if (!index) {
      continue;
    }
    const PlayerMatchState& actual = state.players()[*index];
    PlayerMatchState player = actual;
    player.position = estimatePosition(observation, time, perception);
    player.velocity = observation.velocity;
    player.attributes = PlayerAttributes{};
    player.target = std::nullopt;
    (actual.side == carrier.side ? remembered.teammates : remembered.opponents)
        .push_back({.player = player, .confidence = observation.confidence, .index = *index});
  }
  return remembered;
}

// The line a ball travels along, up to where it matters: straight, or for a
// curled shot an arc that leaves along `direction` and turns by `curvature`
// radians per meter (alongArc()). A ball on it rolls, or for a chip flies
// against the drag (flightSeconds()).
struct Lane {
  SimCore::Vec2 from;
  SimCore::Vec2 direction;
  double speed = 0.0;
  // How far along the line the ball gets before it stops mattering: the
  // receiver takes it, or it reaches the goal.
  double length = 0.0;
  double curvature = 0.0;
  bool flying = false;

  // The point `along` meters down the lane.
  [[nodiscard]] SimCore::Vec2 pointAt(const double along) const noexcept {
    return from + alongArc(direction, along, curvature).offset;
  }
};

// Seconds the player needs to come within `radius` of a point: his arrival
// time less the radius covered at full speed. Empty if the point is off the
// pitch.
[[nodiscard]] inline std::optional<double> reachSeconds(const PlayerMatchState& player,
                                                        const SimCore::Vec2 point,
                                                        const Pitch& pitch, const double radius) {
  const auto arrival = estimateArrivalSeconds(player, point, pitch);
  if (!arrival) {
    return std::nullopt;
  }
  return *arrival - (radius / player.attributes.maxSpeed);
}

// A point of a lane where the players' reach is checked: where it is, how far
// down the lane, and when the ball gets there.
struct LaneSample {
  SimCore::Vec2 point;
  double along = 0.0;
  double ballSeconds = 0.0;
};

// Every kLaneStep along the lane, short of its end. Computed once per lane, so
// every player is checked against the same points.
[[nodiscard]] inline std::vector<LaneSample> laneSamples(const Lane& lane,
                                                         const BallPhysics& ball) {
  std::vector<LaneSample> samples;
  // The samples are the running sum of kLaneStep, not step * kLaneStep: IEEE
  // addition is deterministic on every platform, and the exact sample points
  // are what the pinned scenario results were recorded with.
  // NOLINTNEXTLINE(bugprone-float-loop-counter)
  for (double along = kLaneStep; along < lane.length; along += kLaneStep) {
    samples.push_back({.point = lane.pointAt(along),
                       .along = along,
                       .ballSeconds = lane.flying ? flightSeconds(along, lane.speed, ball)
                                                  : ballSeconds(along, lane.speed, ball)});
  }
  return samples;
}

// The least time a player has to spare reaching the ball at any of the
// samples, negative if he gets there before it; infinite if he reaches none.
// secondsTo(sample, index) is how long he needs to reach the ball at the
// sample at this index, empty where he cannot.
template <typename SecondsTo>
[[nodiscard]] double samplesMargin(const std::vector<LaneSample>& samples,
                                   const SecondsTo& secondsTo) {
  double margin = std::numeric_limits<double>::infinity();
  for (std::size_t index = 0; index < samples.size(); ++index) {
    const std::optional<double> reach = secondsTo(samples[index], index);
    if (reach) {
      margin = std::min(margin, *reach - samples[index].ballSeconds);
    }
  }
  return margin;
}

// The least time a player has to spare reaching the ball at any point of the
// lane, negative if he gets there before it; infinite if he reaches none.
// secondsTo(point, along) is how long he needs to reach the ball at the point
// `along` meters down the lane, empty where he cannot.
template <typename SecondsTo>
[[nodiscard]] double laneMargin(const Lane& lane, const BallPhysics& ball,
                                const SecondsTo& secondsTo) {
  return samplesMargin(laneSamples(lane, ball),
                       [&secondsTo](const LaneSample& sample, std::size_t /*index*/) {
                         return secondsTo(sample.point, sample.along);
                       });
}

// The risk that a player with this margin gets to the ball: from 1 at a
// margin of -marginSeconds through 1/2 at 0 to 0 at +marginSeconds, weighted
// by how sure the carrier is of him; 0 for a player who reaches none of it.
[[nodiscard]] inline double riskOfMargin(const double margin, const double confidence,
                                         const double marginSeconds) noexcept {
  if (margin == std::numeric_limits<double>::infinity()) {
    return 0.0;
  }
  return confidence * fallingStep(margin / marginSeconds);
}

}  // namespace ElyverseFootball::SimMatch::LaneReach
