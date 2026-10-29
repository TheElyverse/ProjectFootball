#include "matchClock.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <stdexcept>

#include "configChecks.hpp"

namespace ElyverseFootball::SimMatch {
namespace {

using ConfigChecks::isFiniteNonNegative;
using ConfigChecks::isWithin;

// The clock's seconds in a half, and in a minute.
inline constexpr std::int64_t kHalfSeconds = std::int64_t{45} * 60;
inline constexpr std::int64_t kMinuteSeconds = 60;
// A day: longer than any match, and short enough that its ticks fit easily.
inline constexpr double kMaxDurationMinutes = 24.0 * 60.0;
// More added minutes than any half needs; keeps them inside an int.
inline constexpr double kMaxAddedMinutes = 999.0;

}  // namespace

std::string_view matchPeriodName(const MatchPeriod period) noexcept {
  switch (period) {
    case MatchPeriod::kFirstHalf:
      return "firstHalf";
    case MatchPeriod::kSecondHalf:
      return "secondHalf";
    case MatchPeriod::kFullTime:
      return "fullTime";
  }
  return "unknown";
}

void validate(const MatchClockConfig& config, const int ticksPerSecond) {
  const bool valid = isWithin(config.durationMinutes, 0.0, kMaxDurationMinutes) &&
                     ticksPerSecond > 0 && halfTicks(config, ticksPerSecond) >= 1 &&
                     isFiniteNonNegative(config.throwInLostSeconds) &&
                     isFiniteNonNegative(config.goalKickLostSeconds) &&
                     isFiniteNonNegative(config.cornerLostSeconds) &&
                     isFiniteNonNegative(config.kickoffLostSeconds);
  if (!valid) {
    throw std::invalid_argument("match clock: invalid configuration");
  }
}

std::int64_t halfTicks(const MatchClockConfig& config, const int ticksPerSecond) noexcept {
  return std::llround(config.durationMinutes * 30.0 * static_cast<double>(ticksPerSecond));
}

double lostSeconds(const RestartKind kind, const MatchClockConfig& config) noexcept {
  switch (kind) {
    case RestartKind::kThrowIn:
      return config.throwInLostSeconds;
    case RestartKind::kGoalKick:
      return config.goalKickLostSeconds;
    case RestartKind::kCorner:
      return config.cornerLostSeconds;
    case RestartKind::kKickoff:
      return config.kickoffLostSeconds;
  }
  return 0.0;
}

int addedMinutes(const double lostSeconds, const MatchClockConfig& config) noexcept {
  // Ninety shown minutes over the duration's real ones.
  const double shown = lostSeconds * (90.0 / config.durationMinutes);
  return static_cast<int>(
      std::clamp(std::ceil(shown / static_cast<double>(kMinuteSeconds)), 0.0, kMaxAddedMinutes));
}

std::int64_t addedTicks(const int minutes, const MatchClockConfig& config,
                        const int ticksPerSecond) noexcept {
  // A shown minute is a 45th of a half: to the nearest tick, in integers.
  const std::int64_t half = halfTicks(config, ticksPerSecond);
  return ((2 * static_cast<std::int64_t>(minutes) * half) + 45) / 90;
}

ClockReading readClock(const MatchClock& clock, const SimCore::SimTick now,
                       const MatchClockConfig& config, const int ticksPerSecond) noexcept {
  if (clock.period == MatchPeriod::kFullTime) {
    return {.period = clock.period,
            .seconds = 2 * kHalfSeconds,
            .addedSeconds = clock.addedMinutes.value_or(0) * kMinuteSeconds};
  }
  const std::int64_t half = halfTicks(config, ticksPerSecond);
  const std::int64_t elapsed = std::max<std::int64_t>(0, now.value() - clock.periodStart.value());
  const std::int64_t regulation = std::min(elapsed, half);
  const std::int64_t base = clock.period == MatchPeriod::kSecondHalf ? kHalfSeconds : 0;
  return {.period = clock.period,
          .seconds = base + (regulation * kHalfSeconds / half),
          .addedSeconds = (elapsed - regulation) * kHalfSeconds / half};
}

std::string formatClock(const ClockReading& reading) {
  const auto clock = [](const std::int64_t seconds) {
    return std::format("{:02}:{:02}", seconds / kMinuteSeconds, seconds % kMinuteSeconds);
  };
  if (reading.addedSeconds > 0) {
    return std::format("{}+{}", reading.seconds / kMinuteSeconds, clock(reading.addedSeconds));
  }
  return clock(reading.seconds);
}

bool endsChanged(const MatchPeriod period) noexcept {
  return period != MatchPeriod::kFirstHalf;
}

}  // namespace ElyverseFootball::SimMatch
