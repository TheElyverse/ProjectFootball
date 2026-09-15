#pragma once

#include <cstdint>

#include "sim_core/strong_id.hpp"

namespace ElyverseFootball::SimCore {

struct PlayerTag {};
struct ClubTag {};
struct MatchTag {};

using PlayerId = StrongId<PlayerTag, std::uint32_t>;
using ClubId = StrongId<ClubTag, std::uint32_t>;
using MatchId = StrongId<MatchTag, std::uint64_t>;

}  // namespace ElyverseFootball::SimCore
