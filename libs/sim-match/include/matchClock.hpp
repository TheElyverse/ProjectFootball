#pragma once

#include <cstdint>
#include <string>

#include "matchState.hpp"
#include "restartKind.hpp"
#include "simTime.hpp"

namespace ElyverseFootball::SimMatch {

// The match clock (docs/match-clock.md): how long a match lasts, who kicks off,
// and the real seconds of play each stoppage costs. Part of MatchConfig and of
// every replay.
struct MatchClockConfig {
  // The real minutes of play both halves last together, without added time.
  // The clock shows them as ninety; the football itself does not change.
  double durationMinutes = 90.0;  // min
  // The side that kicks off the first half; the other kicks off the second.
  TeamSide openingKickoff = TeamSide::kHome;
  // Real seconds of play each restart costs: the half's lost time, which the
  // referee adds at its end. A kickoff after a goal counts the celebration.
  double throwInLostSeconds = 4.0;   // s
  double goalKickLostSeconds = 6.0;  // s
  double cornerLostSeconds = 6.0;    // s
  double kickoffLostSeconds = 30.0;  // s

  friend bool operator==(const MatchClockConfig&, const MatchClockConfig&) = default;
};

// Throws std::invalid_argument for a duration that is not positive and
// finite, or so short that a half lasts no tick at ticksPerSecond, for an
// opening side that is neither home nor away, or for a lost time that is
// negative or not finite.
void validate(const MatchClockConfig& config, int ticksPerSecond);

// The ticks of one half's regulation time: half the duration, to the nearest
// tick.
[[nodiscard]] std::int64_t halfTicks(const MatchClockConfig& config, int ticksPerSecond) noexcept;

// The real seconds of play a restart of this kind costs.
[[nodiscard]] double lostSeconds(RestartKind kind, const MatchClockConfig& config) noexcept;

// The added time for a half that lost these real seconds: as the clock shows
// them, rounded up to whole minutes.
[[nodiscard]] int addedMinutes(double lostSeconds, const MatchClockConfig& config) noexcept;

// The ticks those added minutes last, to the nearest tick.
[[nodiscard]] std::int64_t addedTicks(int minutes, const MatchClockConfig& config,
                                      int ticksPerSecond) noexcept;

// What the clock shows at a tick, in whole seconds of the ninety-minute
// equivalent: the regulation time of the match so far -- up to 45:00 in the
// first half, from 45:00 to 90:00 in the second -- and the added time played on
// top of it. At full time, 90:00 and the second half's added minutes.
struct ClockReading {
  MatchPeriod period = MatchPeriod::kFirstHalf;
  std::int64_t seconds = 0;
  std::int64_t addedSeconds = 0;

  friend bool operator==(const ClockReading&, const ClockReading&) = default;
};

// Exact in whole ticks: a ten-minute match shows at a tick what a ninety-minute
// match shows at nine times that tick.
[[nodiscard]] ClockReading readClock(const MatchClock& clock, SimCore::SimTick now,
                                     const MatchClockConfig& config, int ticksPerSecond) noexcept;

// "mm:ss", with the added time as "45+mm:ss" or "90+mm:ss".
[[nodiscard]] std::string formatClock(const ClockReading& reading);

// Whether the teams have changed ends: in the second half and at full time.
// The engine keeps every side's attacking direction (attackingDirection());
// what changes is the end of the ground it attacks, which only a view shows
// (docs/match-clock.md#halves).
[[nodiscard]] bool endsChanged(MatchPeriod period) noexcept;

}  // namespace ElyverseFootball::SimMatch
