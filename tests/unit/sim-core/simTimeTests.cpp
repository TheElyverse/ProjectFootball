#include <array>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <limits>
#include <stdexcept>

#include "simTime.hpp"

using ElyverseFootball::SimCore::SimClock;

static_assert([] {
  SimClock clock(0.5);
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
