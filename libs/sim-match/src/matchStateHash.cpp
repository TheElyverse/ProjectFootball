#include "matchStateHash.hpp"

#include <optional>

#include "stableHash.hpp"
#include "vec2.hpp"

namespace ElyverseFootball::SimMatch {
namespace {

using SimCore::StableHasher;
using SimCore::Vec2;

void addVec2(StableHasher& hasher, const Vec2 vector) noexcept {
  hasher.addDouble(vector.x);
  hasher.addDouble(vector.y);
}

// A presence flag first, so an empty optional and one holding zeros differ.
void addOptionalVec2(StableHasher& hasher, const std::optional<Vec2>& vector) noexcept {
  hasher.addBool(vector.has_value());
  if (vector) {
    addVec2(hasher, *vector);
  }
}

void addPlayer(StableHasher& hasher, const PlayerMatchState& player) noexcept {
  hasher.addU64(player.playerId.value());
  hasher.addU64(static_cast<std::uint64_t>(player.side));
  addVec2(hasher, player.position);
  addVec2(hasher, player.velocity);
  hasher.addDouble(player.attributes.maxSpeed);
  hasher.addDouble(player.attributes.acceleration);
  addOptionalVec2(hasher, player.target);
  addVec2(hasher, player.facing);
}

}  // namespace

std::uint64_t hashMatchState(const MatchState& state) noexcept {
  StableHasher hasher;
  hasher.addDouble(state.pitch().lengthMeters());
  hasher.addDouble(state.pitch().widthMeters());
  hasher.addI64(state.playersPerSide());
  hasher.addU64(state.players().size());
  for (const PlayerMatchState& player : state.players()) {
    addPlayer(hasher, player);
  }
  addVec2(hasher, state.ball().position);
  addVec2(hasher, state.ball().velocity);
  return hasher.value();
}

}  // namespace ElyverseFootball::SimMatch
