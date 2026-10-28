#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <limits>
#include <numbers>
#include <vector>

#include "stableHash.hpp"
#include "stableMath.hpp"

using ElyverseFootball::SimCore::kMaxStableAngle;
using ElyverseFootball::SimCore::stableArcTangent;
using ElyverseFootball::SimCore::stableCosine;
using ElyverseFootball::SimCore::stableExp;
using ElyverseFootball::SimCore::StableHasher;
using ElyverseFootball::SimCore::stableHypot;
using ElyverseFootball::SimCore::stableLerp;
using ElyverseFootball::SimCore::stableSine;
using ElyverseFootball::SimCore::stableSineCosine;

TEST_CASE("stableExp agrees with std::exp to the last bits", "[stableMath]") {
  REQUIRE(stableExp(0.0) == 1.0);
  // -40 to 40 in steps of 0.0137, counted in integers so the loop's bound does
  // not depend on accumulated rounding.
  for (int step = -2919; step <= 2919; ++step) {
    const double value = static_cast<double>(step) * 0.0137;
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

TEST_CASE("stableLerp goes the amount of the way from one value toward the other", "[stableMath]") {
  REQUIRE(stableLerp(2.0, 6.0, 0.0) == 2.0);
  REQUIRE(stableLerp(2.0, 6.0, 0.25) == 3.0);
  REQUIRE(stableLerp(2.0, 6.0, 1.0) == 6.0);
  REQUIRE(stableLerp(6.0, 2.0, 0.25) == 5.0);
  REQUIRE(stableLerp(2.0, 6.0, 1.5) == 8.0);
}

TEST_CASE("stableSineCosine agrees with std::sin and std::cos to the last bits", "[stableMath]") {
  constexpr double kEpsilon = std::numeric_limits<double>::epsilon();
  // Within a quarter turn either side of zero no reduction happens: relative accuracy.
  for (int step = -1000; step <= 1000; ++step) {
    const double angle = static_cast<double>(step) * (std::numbers::pi / 4.0) / 1000.0;
    CAPTURE(angle);
    const auto [sine, cosine] = stableSineCosine(angle);
    REQUIRE(std::abs(sine - std::sin(angle)) <= 2.0 * kEpsilon * std::abs(std::sin(angle)));
    REQUIRE(std::abs(cosine - std::cos(angle)) <= 2.0 * kEpsilon * std::abs(std::cos(angle)));
  }
  // Beyond it, -1000 to 1000 rad and around a million: within two units in the
  // last place of 1.
  for (int step = -73000; step <= 73000; ++step) {
    const double angle = static_cast<double>(step) * 0.0137;
    CAPTURE(angle);
    const auto [sine, cosine] = stableSineCosine(angle);
    REQUIRE(std::abs(sine - std::sin(angle)) <= 2.0 * kEpsilon);
    REQUIRE(std::abs(cosine - std::cos(angle)) <= 2.0 * kEpsilon);
  }
  for (const double angle : {1e6, -1e6 - 0.5, 1.6e6, kMaxStableAngle, -kMaxStableAngle}) {
    CAPTURE(angle);
    REQUIRE(std::abs(stableSine(angle) - std::sin(angle)) <= 2.0 * kEpsilon);
    REQUIRE(std::abs(stableCosine(angle) - std::cos(angle)) <= 2.0 * kEpsilon);
  }
}

TEST_CASE("stableSineCosine handles the edges like std::sin and std::cos", "[stableMath]") {
  REQUIRE(stableSine(0.0) == 0.0);
  REQUIRE(stableCosine(0.0) == 1.0);
  REQUIRE(std::signbit(stableSine(-0.0)));
  REQUIRE(stableSine(std::numeric_limits<double>::denorm_min()) ==
          std::numeric_limits<double>::denorm_min());
  REQUIRE(stableSine(std::numbers::pi / 2.0) == 1.0);
  REQUIRE(stableCosine(std::numbers::pi) == -1.0);
  REQUIRE(std::isnan(stableSine(std::numeric_limits<double>::quiet_NaN())));
  REQUIRE(std::isnan(stableCosine(std::numeric_limits<double>::infinity())));
  REQUIRE(std::isnan(stableSineCosine(-std::numeric_limits<double>::infinity()).sine));
  // Beyond the range the reduction is exact for, no answer rather than a wrong one.
  for (const double angle : {std::nextafter(kMaxStableAngle, 2e6), -2e6, 1.5e19, 1e300}) {
    CAPTURE(angle);
    REQUIRE(std::isnan(stableSine(angle)));
    REQUIRE(std::isnan(stableCosine(angle)));
  }
}

TEST_CASE("stableSineCosine is odd in the sine and even in the cosine", "[stableMath]") {
  // Odd multiples of pi/4 lie halfway between two quarter turns, where a
  // rounding that is not symmetric would reduce +x and -x differently.
  std::vector<double> angles;
  for (int step = 0; step <= 2000; ++step) {
    angles.push_back(static_cast<double>(step) * 0.0731);
  }
  for (int eighth = 1; eighth <= 41; eighth += 2) {
    angles.push_back(static_cast<double>(eighth) * (std::numbers::pi / 4.0));
  }
  for (const double angle : angles) {
    CAPTURE(angle);
    REQUIRE(stableSine(-angle) == -stableSine(angle));
    REQUIRE(stableCosine(-angle) == stableCosine(angle));
    const auto [sine, cosine] = stableSineCosine(angle);
    REQUIRE(std::abs((sine * sine) + (cosine * cosine) - 1.0) <=
            4.0 * std::numeric_limits<double>::epsilon());
  }
}

TEST_CASE("stableSineCosine is pinned for values the ball physics depend on", "[stableMath]") {
  // Golden values: a change to the algorithm changes every recorded replay.
  REQUIRE(stableSine(1.0) == 0.8414709848078965);
  REQUIRE(stableCosine(1.0) == 0.5403023058681398);
  REQUIRE(stableSine(-2.5) == -0.5984721441039564);
  REQUIRE(stableCosine(10.0) == -0.8390715290764524);
  REQUIRE(stableSine(std::numbers::pi) == 1.2246467991473532e-16);
  REQUIRE(stableCosine(std::numbers::pi / 2.0) == 6.123233995736766e-17);
}

TEST_CASE("stableSineCosine is pinned bit for bit over a dense sweep", "[stableMath]") {
  // Every bit of 20,000 results in one hash: a platform, compiler or flag that
  // changes a single last bit -- floating-point contraction into FMAs, for
  // instance -- fails here, even where the golden values above still hold.
  StableHasher hasher;
  for (int step = -8000; step <= 8000; ++step) {
    const auto [sine, cosine] = stableSineCosine(static_cast<double>(step) * 0.000785);
    hasher.addDouble(sine);
    hasher.addDouble(cosine);
  }
  for (int step = 1; step <= 2000; ++step) {
    const auto [sine, cosine] = stableSineCosine(static_cast<double>(step) * 789.123);
    hasher.addDouble(sine);
    hasher.addDouble(cosine);
  }
  REQUIRE(hasher.value() == 0xfe03d14cf5f7ce3bULL);
}
