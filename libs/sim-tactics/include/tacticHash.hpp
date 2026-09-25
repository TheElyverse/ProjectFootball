#pragma once

#include <cstdint>

#include "stableHash.hpp"
#include "tactic.hpp"

namespace ElyverseFootball::SimTactics {

// Feeds every field of the tactic, in a fixed order, into a stable hash:
// name and description, the slots with their positions and
// responsibilities, the principles and every phase instruction. Two tactics
// hash alike exactly when they are equal, on every platform, so a replay can
// record which tactic a team played and a state hash can include it.
void addTactic(SimCore::StableHasher& hasher, const Tactic& tactic) noexcept;

// addTactic() into a fresh hasher: the tactic's content hash. It depends on
// the model only, not on how a file spelled it -- whitespace, key order or a
// role preset written out as its responsibilities give the same hash.
[[nodiscard]] std::uint64_t contentHash(const Tactic& tactic) noexcept;

}  // namespace ElyverseFootball::SimTactics
