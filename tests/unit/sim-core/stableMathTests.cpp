#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <limits>

#include "stableMath.hpp"

using ElyverseFootball::SimCore::stableExp;

TEST_CASE("stableExp agrees with std::exp to the last bits", "[stableMath]") {
  REQUIRE(stableExp(0.0) == 1.0);
  for (double value = -40.0; value <= 40.0; value += 0.0137) {
    CAPTURE(value);
    const double expected = std::exp(value);
    REQUIRE(std::abs(stableExp(value) - expected) <=
            4.0 * std::numeric_limits<double>::epsilon() * expected);
  }
  for (const double value : {-700.0, -300.5, 1e-12, -1e-12, 300.25, 709.0}) {
    CAPTURE(value);
    const double expected = std::exp(value);
    REQUIRE(std::abs(stableExp(value) - expected) <=
            4.0 * std::numeric_limits<double>::epsilon() * expected);
  }
}

TEST_CASE("stableExp handles the edges like std::exp", "[stableMath]") {
  REQUIRE(stableExp(1000.0) == std::numeric_limits<double>::infinity());
  REQUIRE(stableExp(-1000.0) == 0.0);
  REQUIRE(std::isnan(stableExp(std::numeric_limits<double>::quiet_NaN())));
}

TEST_CASE("stableExp is pinned for values decisions depend on", "[stableMath]") {
  // Golden values: a change to the algorithm changes every recorded replay.
  REQUIRE(stableExp(1.0) == 2.7182818284590455);
  REQUIRE(stableExp(-2.5) == 0.0820849986238988);
}
