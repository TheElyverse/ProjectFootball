#pragma once

#include <string_view>

#include "ballPhysics.hpp"
#include "fitness.hpp"
#include "matchClock.hpp"
#include "matchSimulation.hpp"

namespace ElyverseFootball::SimMatch {

inline constexpr std::string_view kMatchClockSystemName = "match clock";

// What the match clock system is configured with: the default configuration
// for whatever a caller leaves out.
struct MatchClockRules {
  MatchClockConfig clock{};
  FitnessConfig fitness{};
  BallPhysics physics{};
  int ticksPerSecond = kDefaultTicksPerSecond;
  // Whether the second half's kickoff is a set piece: when the match takes
  // restarts, and so runs the set piece system.
  bool setPieces = false;
};

// Every tick (docs/match-clock.md): first every player's fitness one tick on
// with stepFitness(), at the speed he ran in the tick before. Then the clock.
// Once a half's regulation time -- halfTicks() since it started -- is up, the
// added time is announced: addedMinutes() of its lost time, which no stoppage
// after that changes. Once the added time is up too, no shot is on its way, no
// penalty waits to be taken and no foul was committed in the step, the half
// ends with PeriodEnded. After the first half the
// second starts in the same tick with away's kickoff -- home kicks off the first, as every fixture
// has it: both sides lined up as lineUpForKickoff() and takeKickoffLineUp() have them, the ball at
// rest at the taker's feet, PossessionChanged if it was someone else's. A restart taken in that
// tick is overwritten. After the second it is full time. Throws std::invalid_argument for an
// invalid configuration.
[[nodiscard]] MatchSystem makeMatchClockSystem(const MatchClockRules& rules = {});

}  // namespace ElyverseFootball::SimMatch
