#pragma once

#include <cstdint>

#include "strongId.hpp"

namespace ElyverseFootball::SimCore {

struct PlayerTag {};
struct ClubTag {};
struct MatchTag {};

using PlayerId = StrongId<PlayerTag>;
using ClubId = StrongId<ClubTag>;
using MatchId = StrongId<MatchTag, std::uint64_t>;

}  // namespace ElyverseFootball::SimCore
