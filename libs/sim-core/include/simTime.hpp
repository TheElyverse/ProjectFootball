#pragma once

#include <compare>
#include <cstdint>

namespace ElyverseFootball::SimCore {

// A single discrete simulation step. Whether it represents a match tick or a
// world-simulation step depends on which SimClock produced it.
class SimTick {
 public:
  using ValueType = std::int64_t;

  constexpr SimTick() noexcept = default;
  constexpr explicit SimTick(ValueType value) noexcept : value_(value) {}

  [[nodiscard]] constexpr ValueType value() const noexcept { return value_; }

  constexpr SimTick& operator++() noexcept {
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
class SimClock {
 public:
  constexpr explicit SimClock(double secondsPerTick) noexcept : secondsPerTick_(secondsPerTick) {}

  [[nodiscard]] constexpr SimTick tick() const noexcept { return current_; }
  [[nodiscard]] constexpr double secondsPerTick() const noexcept { return secondsPerTick_; }
  [[nodiscard]] constexpr double elapsedSeconds() const noexcept {
    return static_cast<double>(current_.value()) * secondsPerTick_;
  }

  constexpr SimTick advance() noexcept {
    ++current_;
    return current_;
  }

 private:
  SimTick current_;
  double secondsPerTick_;
};

}  // namespace ElyverseFootball::SimCore
