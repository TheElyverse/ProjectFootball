#pragma once

#include <compare>
#include <cstdint>
#include <vector>

#include "ids.hpp"
#include "simTime.hpp"
#include "vec2.hpp"

namespace ElyverseFootball::SimMatch {

// What an observation is about: the ball, or a player by id. Ordered ball
// first, then players by id, which is the order a player's memory keeps.
class ObservedEntity {
 public:
  [[nodiscard]] static constexpr ObservedEntity ball() noexcept {
    return {Kind::kBall, SimCore::PlayerId::invalid()};
  }
  [[nodiscard]] static constexpr ObservedEntity player(const SimCore::PlayerId playerId) noexcept {
    return {Kind::kPlayer, playerId};
  }

  [[nodiscard]] constexpr bool isBall() const noexcept { return kind_ == Kind::kBall; }
  // Invalid for the ball.
  [[nodiscard]] constexpr SimCore::PlayerId playerId() const noexcept { return playerId_; }

  friend constexpr auto operator<=>(const ObservedEntity&, const ObservedEntity&) = default;

 private:
  enum class Kind : std::uint8_t {
    kBall,
    kPlayer,
  };

  constexpr ObservedEntity(const Kind kind, const SimCore::PlayerId playerId) noexcept
      : kind_(kind), playerId_(playerId) {}

  Kind kind_;
  SimCore::PlayerId playerId_;
};

// What a player believes about one entity. position and velocity are the
// entity's as last seen; confidence falls from 1 when seen to 0 when the
// observation is forgotten (docs/perception.md). An estimate for a later tick
// extrapolates from these values, see estimatePosition() in perception.hpp.
struct Observation {
  ObservedEntity entity = ObservedEntity::ball();
  SimCore::Vec2 position;
  SimCore::Vec2 velocity;
  double confidence = 0.0;
  SimCore::SimTick lastSeen;

  friend bool operator==(const Observation&, const Observation&) = default;
};

// A player's memory of the match: one observation per entity he remembers,
// ordered by entity (ball first, then players by id). Never contains the
// player himself.
struct PlayerPerception {
  std::vector<Observation> observations;

  // The observation of an entity, or nullptr if he does not remember it.
  [[nodiscard]] const Observation* find(const ObservedEntity entity) const noexcept {
    for (const Observation& observation : observations) {
      if (observation.entity == entity) {
        return &observation;
      }
    }
    return nullptr;
  }

  friend bool operator==(const PlayerPerception&, const PlayerPerception&) = default;
};

}  // namespace ElyverseFootball::SimMatch
