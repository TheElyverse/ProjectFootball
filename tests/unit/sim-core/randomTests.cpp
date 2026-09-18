#include <array>
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

// Golden vectors for deriveSeed(). Calling the same pure function twice and
// comparing the results (as a prior version of this test did) is tautological
// -- it passes even if the mixing algorithm changes, which would silently
// change every derived stream and invalidate existing replays without any
// test catching it. These checked-in expected seeds are the compatibility
// baseline: a change to any of them means deriveSeed()'s algorithm changed
// and every recorded replay is now invalid (see docs/implementation-plan.md
// section 5.2).
TEST_CASE("deriveSeed matches golden vectors", "[rng][random]") {
  constexpr auto master = std::uint64_t{7};
  REQUIRE(deriveSeed(master, RandomNumberGeneratorDomain::kExecution) == 7191089600892374487ULL);
  REQUIRE(deriveSeed(master, RandomNumberGeneratorDomain::kInjuries) == 309689372594955804ULL);
  REQUIRE(deriveSeed(master, RandomNumberGeneratorDomain::kGeneration) == 16616101746815609346ULL);
  REQUIRE(deriveSeed(master, RandomNumberGeneratorDomain::kMarket) == 10753165928301472203ULL);
  REQUIRE(deriveSeed(master, RandomNumberGeneratorDomain::kAi) == 8346079845500723674ULL);

  REQUIRE(deriveSeed(0, RandomNumberGeneratorDomain::kExecution) == 16294208416658607535ULL);
  REQUIRE(deriveSeed(0xFFFFFFFFFFFFFFFFULL, RandomNumberGeneratorDomain::kAi) ==
          13015481187462834606ULL);
}

// Golden vectors for RandomNumberGenerator::nextUniform(seed = 1234567). These
// lock the owned engine-bits-to-double mapping bit-for-bit: unlike
// std::uniform_real_distribution (whose bit-to-value algorithm is
// implementation-defined and differs between libstdc++/MSVC STL/libc++),
// nextUniform() must produce the exact same doubles on every platform for a
// replay to be reproducible (see docs/implementation-plan.md section 5.2). A
// change to this test's expected values means the mapping changed and every
// recorded replay using nextUniform() is now invalid.
TEST_CASE("RandomNumberGenerator::nextUniform matches golden vectors", "[rng][random]") {
  RandomNumberGenerator rng(1234567);

  REQUIRE(rng.nextUniform() == 0x1.f8653abf0a075p-1);
  REQUIRE(rng.nextUniform() == 0x1.88babbc03b9fbp-1);
  REQUIRE(rng.nextUniform() == 0x1.4ff6f5878925p-1);
  REQUIRE(rng.nextUniform() == 0x1.b09025a66c9ap-5);
  REQUIRE(rng.nextUniform() == 0x1.62679fb358a3p-4);
}

TEST_CASE("RandomNumberGenerator::nextUniform stays within [0, 1)", "[rng][random]") {
  RandomNumberGenerator rng(99);
  for (int i = 0; i < 10000; ++i) {
    const double value = rng.nextUniform();
    REQUIRE(value >= 0.0);
    REQUIRE(value < 1.0);
  }
}

// Golden vectors for RandomNumberGenerator::nextInt(). Same rationale as the
// nextUniform() golden vectors above: std::uniform_int_distribution's bit-to-
// value mapping is implementation-defined, so nextInt() owns a Lemire-based
// mapping instead. A change to these expected values means the mapping
// changed and every recorded replay using nextInt() is now invalid.
TEST_CASE("RandomNumberGenerator::nextInt matches golden vectors", "[rng][random]") {
  RandomNumberGenerator rng(1234567);
  const std::array<int, 10> expected{9, 7, 6, 0, 0, 0, 4, 2, 0, 0};
  for (const int value : expected) {
    REQUIRE(rng.nextInt(0, 9) == value);
  }
}

TEST_CASE("RandomNumberGenerator::nextInt matches golden vectors for a negative range",
          "[rng][random]") {
  RandomNumberGenerator rng(42);
  const std::array<int, 10> expected{3, 2, 3, -4, 4, -4, 1, -1, -2, -1};
  for (const int value : expected) {
    REQUIRE(rng.nextInt(-5, 5) == value);
  }
}

TEST_CASE("RandomNumberGenerator::nextInt stays within [min, max] and is unbiased enough",
          "[rng][random]") {
  RandomNumberGenerator rng(2024);
  std::array<int, 7> counts{};
  for (int i = 0; i < 70000; ++i) {
    const int value = rng.nextInt(0, 6);
    REQUIRE(value >= 0);
    REQUIRE(value <= 6);
    ++counts[static_cast<std::size_t>(value)];
  }
  for (const int count : counts) {
    REQUIRE(count > 8000);
  }
}

TEST_CASE("RandomNumberGenerator::nextInt handles a singleton range", "[rng][random]") {
  RandomNumberGenerator rng(99);
  REQUIRE(rng.nextInt(7, 7) == 7);
  REQUIRE(rng.nextInt(7, 7) == 7);
  REQUIRE(rng.nextInt(7, 7) == 7);
}
