#pragma once

#include <cstdint>
#include <random>

namespace ElyverseFootball::SimCore {

// Per-domain random number generator streams, so that e.g. adding a new injury-
// roll call site doesn't perturb the execution-noise sequence for existing
// replays. See docs/implementation-plan.md section 5.2.
enum class RandomNumberGeneratorDomain : std::uint8_t {
  kExecution,
  kInjuries,
  kGeneration,
  kMarket,
  kAi,
};

// Deterministically derives a per-domain seed from a master seed, so a single
// master seed fully determines every stream without them being correlated.
[[nodiscard]] std::uint64_t deriveSeed(std::uint64_t masterSeed,
                                       RandomNumberGeneratorDomain domain) noexcept;

// Thin wrapper around a deterministic PRNG. Never seed this from a
// non-deterministic source (e.g. std::random_device) inside the simulation
// core -- see docs/implementation-plan.md section 5.2.
class RandomNumberGenerator {
 public:
  explicit RandomNumberGenerator(const std::uint64_t seed) noexcept : engine_(seed) {}

  [[nodiscard]] std::uint64_t nextU64() noexcept { return engine_(); }

  // Owned bit-to-double mapping instead of std::uniform_real_distribution:
  // the standard leaves the engine-bits-to-value algorithm implementation-
  // defined, so libstdc++/MSVC STL/libc++ can produce different doubles from
  // the same engine state and seed. That breaks the deterministic-core
  // replay contract (same seed + commands must reproduce bit-identically
  // across platforms, see docs/implementation-plan.md section 5.2). This
  // takes the top 53 bits of a 64-bit draw (the full mantissa precision of a
  // double) and scales them into [0, 1) with a single IEEE-754 multiply,
  // which is required to be correctly rounded and therefore bit-identical on
  // any conforming platform. Locked by golden vectors in randomTests.cpp --
  // changing this mapping changes every recorded replay.
  [[nodiscard]] double nextUniform() noexcept {
    constexpr double kInverseTwoPow53 = 1.0 / 9007199254740992.0;  // 2^-53
    return static_cast<double>(engine_() >> 11U) * kInverseTwoPow53;
  }

  // Owned bounded-integer mapping instead of std::uniform_int_distribution:
  // like nextUniform() above, the standard leaves its engine-bits-to-value
  // algorithm implementation-defined, which breaks the deterministic-core
  // replay contract across libstdc++/MSVC STL/libc++. This uses Lemire's
  // rejection-sampling method (widening multiply, reject below the bias
  // threshold) on top of a portable 64x64->128 multiply, so it is bit-
  // identical on any conforming platform without relying on a compiler-
  // specific 128-bit type. Locked by golden vectors in randomTests.cpp --
  // changing this mapping changes every recorded replay. Precondition:
  // minInclusive <= maxInclusive (unchecked, same as the previous
  // std::uniform_int_distribution-based implementation).
  [[nodiscard]] int nextInt(int minInclusive, int maxInclusive) noexcept {
    const auto range =
        static_cast<std::uint64_t>(maxInclusive) - static_cast<std::uint64_t>(minInclusive) + 1U;
    const std::uint64_t offset = boundedUniformU64(range);
    return static_cast<int>(static_cast<std::uint64_t>(minInclusive) + offset);
  }

 private:
  struct WideProduct {
    std::uint64_t high;
    std::uint64_t low;
  };

  // Portable (no __int128 / compiler intrinsics) 64x64->128 unsigned
  // multiply via 32-bit limbs, so the result is identical on every platform
  // and toolchain.
  [[nodiscard]] static WideProduct multiplyWide(const std::uint64_t lhs,
                                                const std::uint64_t rhs) noexcept {
    const std::uint64_t aLo = static_cast<std::uint32_t>(lhs);
    const std::uint64_t aHi = lhs >> 32U;
    const std::uint64_t bLo = static_cast<std::uint32_t>(rhs);
    const std::uint64_t bHi = rhs >> 32U;

    const std::uint64_t loLo = aLo * bLo;
    const std::uint64_t hiLo = aHi * bLo;
    const std::uint64_t loHi = aLo * bHi;
    const std::uint64_t hiHi = aHi * bHi;

    const std::uint64_t cross =
        (loLo >> 32U) + static_cast<std::uint32_t>(hiLo) + static_cast<std::uint32_t>(loHi);

    const std::uint64_t high = hiHi + (hiLo >> 32U) + (loHi >> 32U) + (cross >> 32U);
    const std::uint64_t low = (cross << 32U) | static_cast<std::uint32_t>(loLo);
    return {.high = high, .low = low};
  }

  // Lemire's unbiased bounded-random-integer method: maps a raw 64-bit draw
  // to [0, range) without modulo bias, re-drawing only in the (rare) case the
  // low half of the product falls below the bias-rejection threshold.
  [[nodiscard]] std::uint64_t boundedUniformU64(const std::uint64_t range) noexcept {
    WideProduct product = multiplyWide(engine_(), range);
    if (product.low < range) {
      const std::uint64_t threshold = (0ULL - range) % range;
      while (product.low < threshold) {
        product = multiplyWide(engine_(), range);
      }
    }
    return product.high;
  }

  std::mt19937_64 engine_;
};

}  // namespace ElyverseFootball::SimCore
