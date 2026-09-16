#include <catch2/catch_test_macros.hpp>
#include <unordered_set>

#include "ids.hpp"

using ElyverseFootball::SimCore::PlayerId;

TEST_CASE("StrongId compares by value", "[strongId]") {
  REQUIRE(PlayerId(1) == PlayerId(1));
  REQUIRE(PlayerId(1) != PlayerId(2));
}

TEST_CASE("StrongId default-constructs invalid", "[strongId]") {
  REQUIRE_FALSE(PlayerId{}.isValid());
  REQUIRE(PlayerId(7).isValid());
}

TEST_CASE("StrongId is hashable and usable in unordered containers", "[strongId]") {
  const std::unordered_set<PlayerId> ids{PlayerId(1), PlayerId(2), PlayerId(1)};
  REQUIRE(ids.size() == 2);
}
