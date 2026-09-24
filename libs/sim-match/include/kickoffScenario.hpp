#pragma once

#include <expected>
#include <vector>

#include "matchState.hpp"
#include "pitch.hpp"

namespace ElyverseFootball::SimMatch {

// The fixed seven-a-side kickoff fixture of milestone M0.
//
// A pure function of the pitch: no seed, no random number generator, no clock.
// The same pitch always yields the same state, which is what makes it usable as
// the InitialSnapshot of a replay (docs/implementation-plan.md section 5.3).
//
// Home defends x = 0 and attacks +x; away is home's mirror image through the
// halfway line. Player ids are 1..7 for home and 8..14 for away, in the layout
// order documented in docs/match-state.md. Everyone stands still, and the ball
// rests on the center spot.
//
// The result cannot fail for a pitch that exists: every position is a fraction
// strictly between zero and one of the pitch dimensions, so it lies inside any
// valid pitch. It still goes through MatchState::create() and
// checkStartingPositions(), so there is exactly one validated way to build a
// state and a fixture is held to the same rules as any other starting state.
[[nodiscard]] std::expected<MatchState, std::vector<MatchStateError>> makeSevenASideKickoff(
    const Pitch& pitch);

}  // namespace ElyverseFootball::SimMatch
