#pragma once

#include <bit>
#include <cstdint>
#include <string_view>

namespace ElyverseFootball::SimCore {

// A 64-bit FNV-1a hash over values fed to it in a fixed order. Replays and
// determinism tests compare these hashes: two runs that feed the same values
// in the same order produce the same hash on every platform, and a single
// differing bit almost certainly changes it.
//
// Not a cryptographic hash, and not std::hash: std::hash is allowed to differ
// between standard libraries and even between runs, which is useless for a
// value written into a replay file.
//
// Every value is fed as its little-endian bytes, independent of the host's
// byte order. A double is fed as its IEEE 754 bit pattern, so 0.0 and -0.0
// hash differently -- the hash checks that two runs computed the same bits,
// not that they are numerically equal.
class StableHasher {
 public:
  static constexpr std::uint64_t kOffsetBasis = 14695981039346656037ULL;
  static constexpr std::uint64_t kPrime = 1099511628211ULL;

  constexpr void addByte(const std::uint8_t byte) noexcept {
    hash_ ^= byte;
    hash_ *= kPrime;
  }

  constexpr void addU64(const std::uint64_t value) noexcept {
    for (unsigned shift = 0; shift < 64U; shift += 8U) {
      addByte(static_cast<std::uint8_t>(value >> shift));
    }
  }

  constexpr void addI64(const std::int64_t value) noexcept {
    addU64(static_cast<std::uint64_t>(value));
  }

  constexpr void addBool(const bool value) noexcept { addByte(value ? 1U : 0U); }

  constexpr void addDouble(const double value) noexcept {
    addU64(std::bit_cast<std::uint64_t>(value));
  }

  // Length first, so ("ab", "c") and ("a", "bc") hash differently.
  constexpr void addString(const std::string_view text) noexcept {
    addU64(text.size());
    for (const char character : text) {
      addByte(static_cast<std::uint8_t>(character));
    }
  }

  [[nodiscard]] constexpr std::uint64_t value() const noexcept { return hash_; }

 private:
  std::uint64_t hash_ = kOffsetBasis;
};

}  // namespace ElyverseFootball::SimCore
