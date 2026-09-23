#pragma once

#include <compare>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace ElyverseFootball::SimCore {

// A single discrete simulation step. Whether it represents a match tick or a
// world-simulation step depends on which SimClock produced it.
class SimTick {
 public:
  using ValueType = std::int64_t;

  constexpr SimTick() noexcept = default;
  constexpr explicit SimTick(ValueType value) noexcept : value_(value) {}

  [[nodiscard]] constexpr ValueType value() const noexcept { return value_; }

  // Throws std::overflow_error at the maximum tick, leaving the value unchanged.
  constexpr SimTick& operator++() {
    if (value_ == std::numeric_limits<ValueType>::max()) {
      throw std::overflow_error("SimTick: cannot increment the maximum tick");
    }
    ++value_;
    return *this;
  }

  friend constexpr auto operator<=>(const SimTick&, const SimTick&) = default;

 private:
  ValueType value_ = 0;
};

// Fixed-step simulation clock. Advancing the clock is the only way simulation
// time moves forward -- there is no wall-clock/delta-time coupling, so
// replays stay deterministic regardless of host performance. See
// docs/implementation-plan.md section 5.1/5.3.
//
// A clock is either rate-based (withTicksPerSecond(), used by the match
// simulation) or duration-based (the constructor, for steps longer than a
// second such as world-simulation minutes). A rate-based clock computes
// elapsed time as tick / ticksPerSecond: one correctly rounded division, so
// the result is exact whenever the true value is a whole number of seconds and
// otherwise the double nearest to it. Multiplying by a rounded 1/30 instead
// drifts by an ulp for roughly one tick count in eleven (23 ticks, for one).
class SimClock {
 public:
  // Throws std::invalid_argument unless the duration is positive and finite.
  constexpr explicit SimClock(const double secondsPerTick) : secondsPerTick_(secondsPerTick) {
    const bool validDuration =
        secondsPerTick > 0.0 && secondsPerTick <= std::numeric_limits<double>::max();
    if (!validDuration) {
      throw std::invalid_argument("SimClock: secondsPerTick must be positive and finite");
    }
  }

  // Throws std::invalid_argument unless ticksPerSecond is positive.
  [[nodiscard]] static constexpr SimClock withTicksPerSecond(const int ticksPerSecond) {
    if (ticksPerSecond < 1) {
      throw std::invalid_argument("SimClock: ticksPerSecond must be positive");
    }
    SimClock clock(1.0 / static_cast<double>(ticksPerSecond));
    clock.ticksPerSecond_ = ticksPerSecond;
    return clock;
  }

  [[nodiscard]] constexpr SimTick tick() const noexcept { return current_; }
  [[nodiscard]] constexpr double secondsPerTick() const noexcept { return secondsPerTick_; }

  // The whole-number tick rate of a rate-based clock, 0 for a duration-based one.
  [[nodiscard]] constexpr int ticksPerSecond() const noexcept { return ticksPerSecond_; }

  [[nodiscard]] constexpr double elapsedSeconds() const noexcept {
    const auto ticks = static_cast<double>(current_.value());
    if (ticksPerSecond_ > 0) {
      return ticks / static_cast<double>(ticksPerSecond_);
    }
    return ticks * secondsPerTick_;
  }

  // Propagates tick overflow without advancing the clock.
  constexpr SimTick advance() {
    ++current_;
    return current_;
  }

  friend constexpr bool operator==(const SimClock&, const SimClock&) = default;

 private:
  SimTick current_;
  double secondsPerTick_;
  int ticksPerSecond_ = 0;
};

}  // namespace ElyverseFootball::SimCore
