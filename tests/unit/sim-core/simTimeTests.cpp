#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "simTime.hpp"

using ElyverseFootball::SimCore::SimClock;

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
