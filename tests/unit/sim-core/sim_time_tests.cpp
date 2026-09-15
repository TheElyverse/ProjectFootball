#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "sim_core/sim_time.hpp"

using ElyverseFootball::SimCore::SimClock;

TEST_CASE("SimClock advances one tick at a time", "[sim_time]") {
  SimClock clock(1.0 / 30.0);
  REQUIRE(clock.tick().value() == 0);

  clock.advance();
  clock.advance();

  REQUIRE(clock.tick().value() == 2);
}

TEST_CASE("SimClock elapsed_seconds tracks ticks * seconds_per_tick", "[sim_time]") {
  SimClock clock(0.5);
  clock.advance();
  clock.advance();
  clock.advance();

  REQUIRE(clock.elapsed_seconds() == Catch::Approx(1.5));
}
