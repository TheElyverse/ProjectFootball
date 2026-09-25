#include "spatialQueries.hpp"

#include <algorithm>
#include <cmath>

namespace ElyverseFootball::SimMatch {
namespace {

using SimCore::Vec2;

struct Candidate {
  std::size_t index;
  SimCore::PlayerId playerId;
  double distanceSquared;
};

// Nearest first; equal distances by id.
[[nodiscard]] bool closer(const Candidate& left, const Candidate& right) noexcept {
  if (left.distanceSquared != right.distanceSquared) {
    return left.distanceSquared < right.distanceSquared;
  }
  return left.playerId < right.playerId;
}

[[nodiscard]] NearbyPlayer toNearby(const Candidate& candidate) noexcept {
  return {.index = candidate.index,
          .playerId = candidate.playerId,
          .distance = std::sqrt(candidate.distanceSquared)};
}

}  // namespace

std::vector<NearbyPlayer> findPlayersWithin(const MatchState& state, const Vec2 center,
                                            const double radius, const PlayerFilter& filter) {
  // No distance is below a negative radius; squaring it would turn it into a
  // positive one. Written so that NaN finds no one either.
  if (!(radius >= 0.0)) {
    return {};
  }
  const double radiusSquared = radius * radius;
  std::vector<Candidate> candidates;
  for (std::size_t index = 0; const PlayerMatchState& player : state.players()) {
    const double distanceSquared = (player.position - center).lengthSquared();
    if (filter.accepts(player) && distanceSquared <= radiusSquared) {
      candidates.push_back(
          {.index = index, .playerId = player.playerId, .distanceSquared = distanceSquared});
    }
    ++index;
  }
  std::ranges::sort(candidates, closer);

  std::vector<NearbyPlayer> nearby;
  nearby.reserve(candidates.size());
  for (const Candidate& candidate : candidates) {
    nearby.push_back(toNearby(candidate));
  }
  return nearby;
}

std::optional<NearbyPlayer> findNearestPlayer(const MatchState& state, const Vec2 center,
                                              const PlayerFilter& filter) {
  std::optional<Candidate> nearest;
  for (std::size_t index = 0; const PlayerMatchState& player : state.players()) {
    const Candidate candidate{.index = index,
                              .playerId = player.playerId,
                              .distanceSquared = (player.position - center).lengthSquared()};
    if (filter.accepts(player) && (!nearest || closer(candidate, *nearest))) {
      nearest = candidate;
    }
    ++index;
  }
  if (!nearest) {
    return std::nullopt;
  }
  return toNearby(*nearest);
}

std::optional<double> estimateArrivalSeconds(const PlayerMatchState& player, const Vec2 position,
                                             const Pitch& pitch) noexcept {
  if (!pitch.contains(position)) {
    return std::nullopt;
  }
  const Vec2 offset = position - player.position;
  const double distance = std::sqrt(offset.lengthSquared());
  if (distance == 0.0) {
    return 0.0;
  }
  const double acceleration = player.attributes.acceleration;
  const double maxSpeed = player.attributes.maxSpeed;
  // Speed along the way to the position; negative when running away from it.
  const double startSpeed = std::min(player.velocity.dot(offset) / distance, maxSpeed);

  // Accelerating from startSpeed to maxSpeed covers (v² - v0²) / (2a). A
  // position within that distance is reached while still accelerating:
  // d = v0·t + a·t²/2. Beyond it, the rest is run at maxSpeed.
  const double accelerationDistance =
      ((maxSpeed * maxSpeed) - (startSpeed * startSpeed)) / (2.0 * acceleration);
  if (distance <= accelerationDistance) {
    return (-startSpeed + std::sqrt((startSpeed * startSpeed) + (2.0 * acceleration * distance))) /
           acceleration;
  }
  const double accelerationSeconds = (maxSpeed - startSpeed) / acceleration;
  return accelerationSeconds + ((distance - accelerationDistance) / maxSpeed);
}

}  // namespace ElyverseFootball::SimMatch
