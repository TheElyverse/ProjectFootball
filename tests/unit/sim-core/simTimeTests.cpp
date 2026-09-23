#include <array>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <limits>
#include <stdexcept>

#include "simTime.hpp"

using ElyverseFootball::SimCore::SimClock;
using ElyverseFootball::SimCore::SimTick;

static_assert(!noexcept(++SimTick{}));

TEST_CASE("SimTick increments to the maximum tick", "[simTime]") {
  constexpr auto maxTick = std::numeric_limits<SimTick::ValueType>::max();
  SimTick tick(maxTick - 1);

  const auto& incremented = ++tick;
  REQUIRE(&incremented == &tick);
  REQUIRE(tick.value() == maxTick);
}

TEST_CASE("SimTick rejects overflow without mutation", "[simTime]") {
  constexpr auto maxTick = std::numeric_limits<SimTick::ValueType>::max();
  SimTick tick(maxTick);
  REQUIRE_THROWS_AS(++tick, std::overflow_error);
  REQUIRE(tick.value() == maxTick);
}

static_assert([] {
  SimClock clock(0.5);
  static_assert(!noexcept(clock.advance()));
  clock.advance();
  return clock.tick().value() == 1 && clock.elapsedSeconds() == 0.5;
}());

TEST_CASE("SimClock rejects nonpositive and nonfinite tick durations", "[simTime]") {
  constexpr std::array invalidDurations{
      0.0,
      -0.0,
      -0.5,
      std::numeric_limits<double>::lowest(),
      std::numeric_limits<double>::quiet_NaN(),
      std::numeric_limits<double>::infinity(),
      -std::numeric_limits<double>::infinity(),
  };
  for (const double duration : invalidDurations) {
    CAPTURE(duration);
    REQUIRE_THROWS_AS(SimClock(duration), std::invalid_argument);
  }
}

TEST_CASE("SimClock accepts positive finite tick duration boundaries", "[simTime]") {
  constexpr std::array validDurations{
      std::numeric_limits<double>::denorm_min(),
      std::numeric_limits<double>::min(),
      0.5,
      std::numeric_limits<double>::max(),
  };
  for (const double duration : validDurations) {
    CAPTURE(duration);
    SimClock clock(duration);
    REQUIRE(clock.secondsPerTick() == duration);
    REQUIRE(clock.elapsedSeconds() == 0.0);
    clock.advance();
    REQUIRE(clock.elapsedSeconds() == duration);
  }
}

TEST_CASE("SimClock advances one tick at a time", "[simTime]") {
  SimClock clock(1.0 / 30.0);
  REQUIRE(clock.tick().value() == 0);

  clock.advance();
  clock.advance();

  REQUIRE(clock.tick().value() == 2);
}

TEST_CASE("SimClock elapsedSeconds tracks ticks * secondsPerTick", "[simTime]") {
  SimClock clock(0.5);
  clock.advance();
  clock.advance();
  clock.advance();

  REQUIRE(clock.elapsedSeconds() == Catch::Approx(1.5));
}

static_assert([] {
  SimClock clock = SimClock::withTicksPerSecond(4);
  clock.advance();
  return clock.elapsedSeconds() == 0.25;
}());

TEST_CASE("A 30 Hz clock reaches ten seconds after 300 ticks", "[simTime]") {
  SimClock clock = SimClock::withTicksPerSecond(30);
  REQUIRE(clock.ticksPerSecond() == 30);
  REQUIRE(clock.secondsPerTick() == 1.0 / 30.0);

  for (int step = 0; step < 300; ++step) {
    clock.advance();
  }

  REQUIRE(clock.tick().value() == 300);
  REQUIRE(clock.elapsedSeconds() == 10.0);
}

TEST_CASE("A rate-based clock divides instead of multiplying", "[simTime]") {
  // 23 * (1.0 / 30.0) is one ulp away from 23.0 / 30.0; the clock must give
  // the correctly rounded quotient for every tick count, not only for 300.
  REQUIRE(23.0 * (1.0 / 30.0) != 23.0 / 30.0);

  SimClock clock = SimClock::withTicksPerSecond(30);
  for (int tick = 1; tick <= 30 * 60 * 10; ++tick) {
    clock.advance();
    CAPTURE(tick);
    REQUIRE(clock.elapsedSeconds() == static_cast<double>(tick) / 30.0);
  }
}

TEST_CASE("SimClock rejects a nonpositive tick rate", "[simTime]") {
  constexpr std::array invalidRates{0, -1, std::numeric_limits<int>::min()};
  for (const int rate : invalidRates) {
    CAPTURE(rate);
    REQUIRE_THROWS_AS(SimClock::withTicksPerSecond(rate), std::invalid_argument);
  }
}

TEST_CASE("A duration-based clock reports no tick rate", "[simTime]") {
  REQUIRE(SimClock(60.0).ticksPerSecond() == 0);
}
