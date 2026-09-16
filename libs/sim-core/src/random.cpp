#include "random.hpp"

namespace ElyverseFootball::SimCore {

std::uint64_t deriveSeed(std::uint64_t masterSeed, RandomNumberGeneratorDomain domain) noexcept {
  // splitmix64-style mixing of (masterSeed, domain) -- cheap, deterministic,
  // and avoids correlated streams between domains for nearby master seeds.
  std::uint64_t x = masterSeed + 0x9E3779B97F4A7C15ULL * (static_cast<std::uint64_t>(domain) + 1);
  x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
  x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
  x = x ^ (x >> 31);
  return x;
}

}  // namespace ElyverseFootball::SimCore
