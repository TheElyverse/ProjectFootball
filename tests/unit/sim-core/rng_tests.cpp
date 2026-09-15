#include <catch2/catch_test_macros.hpp>
#include <cstdint>

#include "sim_core/rng.hpp"

using ElyverseFootball::SimCore::derive_seed;
using ElyverseFootball::SimCore::Rng;
using ElyverseFootball::SimCore::RngDomain;

TEST_CASE("Rng is deterministic for a given seed", "[rng]") {
  Rng a(42);
  Rng b(42);

  for (int i = 0; i < 100; ++i) {
    REQUIRE(a.next_u64() == b.next_u64());
  }
}

TEST_CASE("derive_seed produces distinct streams per domain", "[rng]") {
  const auto master = std::uint64_t{123456789};

  const auto execution_seed = derive_seed(master, RngDomain::kExecution);
  const auto injuries_seed = derive_seed(master, RngDomain::kInjuries);

  REQUIRE(execution_seed != injuries_seed);
}

TEST_CASE("derive_seed is itself deterministic", "[rng]") {
  const auto master = std::uint64_t{7};
  REQUIRE(derive_seed(master, RngDomain::kMarket) == derive_seed(master, RngDomain::kMarket));
}
