#include <catch2/catch_test_macros.hpp>
#include <cstdint>

#include "random.hpp"

using ElyverseFootball::SimCore::deriveSeed;
using ElyverseFootball::SimCore::RandomNumberGenerator;
using ElyverseFootball::SimCore::RandomNumberGeneratorDomain;

TEST_CASE("RandomNumberGenerator is deterministic for a given seed", "[rng][random]") {
  RandomNumberGenerator rngA(42);
  RandomNumberGenerator rngB(42);

  for (int i = 0; i < 100; ++i) {
    REQUIRE(rngA.nextU64() == rngB.nextU64());
  }
}

TEST_CASE("deriveSeed produces distinct streams per domain", "[rng][random]") {
  constexpr auto master = std::uint64_t{123456789};

  const auto executionSeed = deriveSeed(master, RandomNumberGeneratorDomain::kExecution);
  const auto injuriesSeed = deriveSeed(master, RandomNumberGeneratorDomain::kInjuries);

  REQUIRE(executionSeed != injuriesSeed);
}

TEST_CASE("deriveSeed is itself deterministic", "[rng][random]") {
  constexpr auto master = std::uint64_t{7};
  REQUIRE(deriveSeed(master, RandomNumberGeneratorDomain::kMarket) ==
          deriveSeed(master, RandomNumberGeneratorDomain::kMarket));
}
