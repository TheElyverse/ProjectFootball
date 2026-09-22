#include <array>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <limits>

#include "vec2.hpp"

using ElyverseFootball::SimCore::distance;
using ElyverseFootball::SimCore::Vec2;

TEST_CASE("Vec2 defaults to zero and supports displacement arithmetic", "[vec2]") {
  constexpr Vec2 origin;
  constexpr Vec2 position{.x = 10.0, .y = 20.0};
  constexpr Vec2 displacement{.x = -3.0, .y = 4.0};

  REQUIRE(origin == Vec2{.x = 0.0, .y = 0.0});
  REQUIRE(position + displacement == Vec2{.x = 7.0, .y = 24.0});
  REQUIRE(position - displacement == Vec2{.x = 13.0, .y = 16.0});
  REQUIRE(position - position == origin);
  REQUIRE(position == Vec2{.x = 10.0, .y = 20.0});
}

TEST_CASE("Vec2 scales velocity into displacement", "[vec2]") {
  constexpr Vec2 velocity{.x = 6.0, .y = -8.0};

  REQUIRE(velocity * 0.5 == Vec2{.x = 3.0, .y = -4.0});
  REQUIRE(0.5 * velocity == velocity * 0.5);
  REQUIRE(velocity * 0.0 == Vec2{});
  REQUIRE(velocity * -1.0 == Vec2{.x = -6.0, .y = 8.0});
}

TEST_CASE("Vec2 computes length and dot products", "[vec2]") {
  REQUIRE(Vec2{.x = 3.0, .y = -4.0}.length() == Catch::Approx(5.0));
  REQUIRE(Vec2{}.length() == 0.0);
  REQUIRE(Vec2{.x = 1.0, .y = 0.0}.dot(Vec2{.x = 0.0, .y = 1.0}) == 0.0);
  REQUIRE(Vec2{.x = 3.0, .y = -4.0}.dot(Vec2{.x = -3.0, .y = 4.0}) == -25.0);
}

TEST_CASE("Vec2 computes Euclidean distances", "[vec2]") {
  constexpr Vec2 first{.x = -2.0, .y = 3.0};
  constexpr Vec2 second{.x = 1.0, .y = 7.0};
  REQUIRE(distance(first, second) == Catch::Approx(5.0));
  REQUIRE(distance(first, second) == distance(second, first));
  REQUIRE(distance(first, first) == 0.0);
}

TEST_CASE("Vec2 length avoids overflow and underflow from squaring components", "[vec2]") {
  constexpr Vec2 large{.x = 3.0e200, .y = 4.0e200};
  constexpr Vec2 small{.x = 3.0e-200, .y = 4.0e-200};

  REQUIRE(large.length() / 1.0e200 == Catch::Approx(5.0));
  REQUIRE(small.length() / 1.0e-200 == Catch::Approx(5.0));
}

TEST_CASE("Vec2 identifies nonfinite components", "[vec2]") {
  REQUIRE(Vec2{}.isFinite());
  REQUIRE(Vec2{.x = std::numeric_limits<double>::max(), .y = std::numeric_limits<double>::lowest()}
              .isFinite());

  constexpr std::array invalidValues{
      std::numeric_limits<double>::quiet_NaN(),
      std::numeric_limits<double>::infinity(),
      -std::numeric_limits<double>::infinity(),
  };
  for (const double value : invalidValues) {
    CAPTURE(value);
    REQUIRE_FALSE(Vec2{.x = value, .y = 0.0}.isFinite());
    REQUIRE_FALSE(Vec2{.x = 0.0, .y = value}.isFinite());
  }
}
