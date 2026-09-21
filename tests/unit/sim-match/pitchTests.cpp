#include <array>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <cmath>
#include <limits>
#include <stdexcept>

#include "pitch.hpp"

using ElyverseFootball::SimCore::Vec2;
using ElyverseFootball::SimMatch::Pitch;

TEST_CASE("Pitch preserves configurable dimensions in meters", "[pitch]") {
  const Pitch smallPitch(60.0, 40.0);
  const Pitch largePitch(105.0, 68.0);

  REQUIRE(smallPitch.lengthMeters() == 60.0);
  REQUIRE(smallPitch.widthMeters() == 40.0);
  REQUIRE(largePitch.lengthMeters() == 105.0);
  REQUIRE(largePitch.widthMeters() == 68.0);
  REQUIRE_FALSE(smallPitch.contains({80.0, 50.0}));
  REQUIRE(largePitch.contains({80.0, 50.0}));
}

TEST_CASE("Pitch includes its interior, edges and corners", "[pitch]") {
  const Pitch pitch(60.0, 40.0);
  constexpr std::array points{
      Vec2{.x = 30.0, .y = 20.0}, Vec2{.x = 0.0, .y = 0.0},   Vec2{.x = 60.0, .y = 0.0},
      Vec2{.x = 0.0, .y = 40.0},  Vec2{.x = 60.0, .y = 40.0}, Vec2{.x = 0.0, .y = 20.0},
      Vec2{.x = 60.0, .y = 20.0}, Vec2{.x = 30.0, .y = 0.0},  Vec2{.x = 30.0, .y = 40.0},
  };
  for (const Vec2 point : points) {
    CAPTURE(point.x, point.y);
    REQUIRE(pitch.contains(point));
  }
}

TEST_CASE("Pitch excludes points immediately outside each boundary", "[pitch]") {
  const Pitch pitch(60.0, 40.0);
  const std::array points{
      Vec2{.x = std::nextafter(0.0, -0.1), .y = 20.0},
      Vec2{.x = std::nextafter(60.0, 60.1), .y = 20.0},
      Vec2{.x = 30.0, .y = std::nextafter(0.0, -0.1)},
      Vec2{.x = 30.0, .y = std::nextafter(40.0, 40.1)},
  };
  for (const Vec2 point : points) {
    CAPTURE(point.x, point.y);
    REQUIRE_FALSE(pitch.contains(point));
  }
}

TEST_CASE("Pitch rejects nonpositive and nonfinite dimensions", "[pitch]") {
  const double dimension =
      GENERATE(0.0, -0.0, -1.0, std::numeric_limits<double>::lowest(),
               std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity(),
               -std::numeric_limits<double>::infinity());
  CAPTURE(dimension);
  REQUIRE_THROWS_AS(Pitch(dimension, 40.0), std::invalid_argument);
  REQUIRE_THROWS_AS(Pitch(60.0, dimension), std::invalid_argument);
}

TEST_CASE("Pitch excludes nonfinite positions", "[pitch]") {
  const Pitch pitch(60.0, 40.0);
  constexpr std::array invalidValues{
      std::numeric_limits<double>::quiet_NaN(),
      std::numeric_limits<double>::infinity(),
      -std::numeric_limits<double>::infinity(),
  };
  for (const double value : invalidValues) {
    CAPTURE(value);
    REQUIRE_FALSE(pitch.contains({value, 20.0}));
    REQUIRE_FALSE(pitch.contains({30.0, value}));
  }
}

TEST_CASE("Pitch supports positive finite dimension extremes", "[pitch]") {
  constexpr std::array dimensions{
      std::numeric_limits<double>::denorm_min(),
      std::numeric_limits<double>::min(),
      std::numeric_limits<double>::max(),
  };
  for (const double dimension : dimensions) {
    CAPTURE(dimension);
    const Pitch pitch(dimension, dimension);
    REQUIRE(pitch.contains({0.0, 0.0}));
    REQUIRE(pitch.contains({dimension, dimension}));
  }
}
