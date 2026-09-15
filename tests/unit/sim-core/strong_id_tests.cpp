#include <catch2/catch_test_macros.hpp>
#include <unordered_set>

#include "sim_core/ids.hpp"

using ElyverseFootball::SimCore::PlayerId;

TEST_CASE("StrongId compares by value", "[strong_id]") {
  REQUIRE(PlayerId(1) == PlayerId(1));
  REQUIRE(PlayerId(1) != PlayerId(2));
}

TEST_CASE("StrongId default-constructs invalid", "[strong_id]") {
  REQUIRE_FALSE(PlayerId{}.is_valid());
  REQUIRE(PlayerId(7).is_valid());
}

TEST_CASE("StrongId is hashable and usable in unordered containers", "[strong_id]") {
  const std::unordered_set<PlayerId> ids{PlayerId(1), PlayerId(2), PlayerId(1)};
  REQUIRE(ids.size() == 2);
}
