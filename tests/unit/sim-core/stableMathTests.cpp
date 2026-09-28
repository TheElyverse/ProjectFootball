#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <limits>
#include <numbers>

#include "stableMath.hpp"

using ElyverseFootball::SimCore::stableArcTangent;
using ElyverseFootball::SimCore::stableExp;
using ElyverseFootball::SimCore::stableHypot;

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

TEST_CASE("stableArcTangent agrees with std::atan to the last bits", "[stableMath]") {
  REQUIRE(stableArcTangent(0.0) == 0.0);
  for (int step = -1460; step <= 1460; ++step) {
    const double value = static_cast<double>(step) * 0.0137;
    CAPTURE(value);
    const double expected = std::atan(value);
    REQUIRE(std::abs(stableArcTangent(value) - expected) <=
            4.0 * std::numeric_limits<double>::epsilon() * std::abs(expected));
  }
  for (const double value : {-1e9, -1.0, -1e-9, 1e-12, 0.9999999, 1.0000001, 1e6}) {
    CAPTURE(value);
    const double expected = std::atan(value);
    REQUIRE(std::abs(stableArcTangent(value) - expected) <=
            4.0 * std::numeric_limits<double>::epsilon() * std::abs(expected));
  }
}

TEST_CASE("stableArcTangent handles the edges like std::atan", "[stableMath]") {
  constexpr double kRightAngle = std::numbers::pi / 2.0;
  REQUIRE(stableArcTangent(std::numeric_limits<double>::infinity()) == kRightAngle);
  REQUIRE(stableArcTangent(-std::numeric_limits<double>::infinity()) == -kRightAngle);
  REQUIRE(std::signbit(stableArcTangent(-0.0)));
  REQUIRE(stableArcTangent(-0.0) == 0.0);
  REQUIRE(std::isnan(stableArcTangent(std::numeric_limits<double>::quiet_NaN())));
  REQUIRE(stableArcTangent(std::numeric_limits<double>::denorm_min()) ==
          std::numeric_limits<double>::denorm_min());
}

TEST_CASE("stableArcTangent is odd and monotonic", "[stableMath]") {
  double previous = stableArcTangent(-50.0);
  for (int step = -99; step <= 100; ++step) {
    const double value = static_cast<double>(step) * 0.5;
    CAPTURE(value);
    const double angle = stableArcTangent(value);
    REQUIRE(angle > previous);
    REQUIRE(angle == -stableArcTangent(-value));
    previous = angle;
  }
}

TEST_CASE("stableArcTangent is pinned for values decisions depend on", "[stableMath]") {
  // Golden values: a change to the algorithm changes every recorded replay.
  REQUIRE(stableArcTangent(1.0) == 0.7853981633974483);
  REQUIRE(stableArcTangent(0.5) == 0.4636476090008061);
  REQUIRE(stableArcTangent(3.0) == 1.2490457723982544);
}

TEST_CASE("stableHypot is exactly sqrt(x * x + y * y) where squaring is safe", "[stableMath]") {
  REQUIRE(stableHypot(3.0, 4.0) == 5.0);
  REQUIRE(stableHypot(0.0, 0.0) == 0.0);
  for (int step = -300; step <= 300; ++step) {
    const double x = static_cast<double>(step) * 0.0137;
    const double y = 7.25 - (static_cast<double>(step) * 0.031);
    CAPTURE(x, y);
    REQUIRE(stableHypot(x, y) == std::sqrt((x * x) + (y * y)));
    REQUIRE(stableHypot(x, y) == stableHypot(-y, x));
  }
  for (const double scale : {1e-150, 1e-6, 1e6, 1e150}) {
    CAPTURE(scale);
    const double x = 3.0 * scale;
    const double y = 4.0 * scale;
    REQUIRE(stableHypot(x, y) == std::sqrt((x * x) + (y * y)));
  }
}

TEST_CASE("stableHypot neither overflows nor underflows on extreme components", "[stableMath]") {
  constexpr double kMax = std::numeric_limits<double>::max();
  constexpr double kDenormMin = std::numeric_limits<double>::denorm_min();
  const double tolerance = 4.0 * std::numeric_limits<double>::epsilon();

  REQUIRE(stableHypot(kMax, 0.0) == kMax);
  REQUIRE(stableHypot(0.0, -kMax) == kMax);
  REQUIRE_THAT(stableHypot(kMax / 2.0, kMax / 2.0) / (kMax / 2.0),
               Catch::Matchers::WithinRel(std::numbers::sqrt2, tolerance));
  REQUIRE(stableHypot(kMax, kMax) == std::numeric_limits<double>::infinity());
  REQUIRE_THAT(stableHypot(3e300, 4e300) / 1e300, Catch::Matchers::WithinRel(5.0, tolerance));

  REQUIRE_THAT(stableHypot(3e-300, 4e-300) / 1e-300, Catch::Matchers::WithinRel(5.0, tolerance));
  REQUIRE(stableHypot(1e-200, 1e-200) > 1e-200);
  REQUIRE(stableHypot(kDenormMin, 0.0) == kDenormMin);
  REQUIRE(stableHypot(0.0, -kDenormMin) == kDenormMin);
}

TEST_CASE("stableHypot handles non-finite components like std::hypot", "[stableMath]") {
  constexpr double kInfinity = std::numeric_limits<double>::infinity();
  constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
  REQUIRE(stableHypot(kInfinity, 1.0) == kInfinity);
  REQUIRE(stableHypot(1.0, -kInfinity) == kInfinity);
  REQUIRE(stableHypot(kInfinity, kNaN) == kInfinity);
  REQUIRE(stableHypot(kNaN, -kInfinity) == kInfinity);
  REQUIRE(std::isnan(stableHypot(kNaN, 1.0)));
  REQUIRE(std::isnan(stableHypot(0.0, kNaN)));
}
