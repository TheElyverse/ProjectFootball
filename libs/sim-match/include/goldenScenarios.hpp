#pragma once

#include <cstdint>
#include <expected>
#include <string>
#include <vector>

#include "matchSetup.hpp"
#include "responsibility.hpp"
#include "tactic.hpp"

namespace ElyverseFootball::SimMatch {

// The P2 golden scenarios (docs/golden-scenarios.md): short, hand-placed
// seven-a-side situations in which one tactical behaviour must show. Each
// has an entry in the scenario catalog with the parameters below at their
// defaults; the parameters let a test compare variants of one situation.
// Like every scenario, they are versioned: a change changes every match
// played from them.

// Home has just won the ball in midfield with three attackers against two
// defenders; away's other players are caught upfield.
[[nodiscard]] std::expected<MatchSetup, std::string> makeTransitionThreeVersusTwo(
    std::uint64_t seed);

// A home centre back passes out to his winger on the touchline, far from any
// teammate; away presses on the isolatedReceiver trigger.
[[nodiscard]] std::expected<MatchSetup, std::string> makeIsolatedWinger(std::uint64_t seed);

// Where the pressed receiver of makePressingTrap() stands.
enum class TrapSpot : std::uint8_t {
  kTouchline,
  kCentre,
};

// Away's full-back plays a pass to a teammate facing his own goal, at the
// touchline or in the centre; home presses on receiverFacingOwnGoal with the
// given intensity.
[[nodiscard]] std::expected<MatchSetup, std::string> makePressingTrap(
    std::uint64_t seed, TrapSpot spot = TrapSpot::kTouchline, double pressingIntensity = 1.0);

// Home's midfielder on the ball, his striker level with away's defensive
// line and free to run in behind it. Passes go to where a teammate stands,
// not into space, so the scenario shows the run and the defence's answer to
// it; the pass into the run's space needs a through pass the match does not
// have yet.
[[nodiscard]] std::expected<MatchSetup, std::string> makeRunBehindTheLine(std::uint64_t seed);

// The pressing side's tactic in the golden scenarios: the reference tactic,
// pressing on these triggers with this intensity in every phase, and in its
// pressing phase from this pressing line on -- 1 keeps it out of the
// pressing phase, so only the triggers start presses.
//
// Throws std::invalid_argument for values Tactic::create() rejects: an
// intensity or pressing line outside [0, 1], or unknown or repeated
// triggers.
[[nodiscard]] SimTactics::Tactic goldenPressingTactic(
    double pressingIntensity, const std::vector<SimTactics::PressingTrigger>& triggers,
    double pressingLine);

}  // namespace ElyverseFootball::SimMatch
