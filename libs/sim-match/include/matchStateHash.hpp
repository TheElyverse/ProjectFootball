#pragma once

#include <cstdint>

#include "matchState.hpp"

namespace ElyverseFootball::SimMatch {

// A stable 64-bit hash of every field of a match state, in a fixed order:
// pitch, squad size, then every player in state order, then the ball, then
// every player's perception memory in state order. Two
// states hash equally if and only if they are equal field by field and bit by
// bit (short of an FNV collision). Replays record these hashes at checkpoints
// and compare them on playback.
//
// Every field added to the state must be added here, in the same commit;
// matchStateHashTests.cpp checks that each field changes the hash.
[[nodiscard]] std::uint64_t hashMatchState(const MatchState& state) noexcept;

}  // namespace ElyverseFootball::SimMatch
