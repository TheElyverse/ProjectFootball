#include <catch2/catch_test_macros.hpp>
#include <cstdint>

#include "random.hpp"

using ElyverseFootball::SimCore::deriveSeed;
using ElyverseFootball::SimCore::RandomNumberGeneratorDomain;
using ElyverseFootball::SimCore::Rng;

TEST_CASE("Rng is deterministic for a given seed", "[rng]") {
  Rng a(42);
  Rng b(42);

  for (int i = 0; i < 100; ++i) {
    REQUIRE(a.nextU64() == b.nextU64());
  }
}

TEST_CASE("deriveSeed produces distinct streams per domain", "[rng]") {
  const auto master = std::uint64_t{123456789};

  const auto executionSeed = deriveSeed(master, RandomNumberGeneratorDomain::kExecution);
  const auto injuriesSeed = deriveSeed(master, RandomNumberGeneratorDomain::kInjuries);

  REQUIRE(executionSeed != injuriesSeed);
}

TEST_CASE("deriveSeed is itself deterministic", "[rng]") {
  const auto master = std::uint64_t{7};
  REQUIRE(deriveSeed(master, RandomNumberGeneratorDomain::kMarket) ==
          deriveSeed(master, RandomNumberGeneratorDomain::kMarket));
}
