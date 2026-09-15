#pragma once

#include <cstdint>
#include <random>

namespace ElyverseFootball::SimCore {

// Per-domain RNG streams, so that e.g. adding a new injury-roll call site
// doesn't perturb the execution-noise sequence for existing replays. See
// docs/implementation-plan.md section 5.2.
enum class RngDomain : std::uint32_t {
  kExecution,
  kInjuries,
  kGeneration,
  kMarket,
  kAi,
};

// Deterministically derives a per-domain seed from a master seed, so a single
// master seed fully determines every stream without them being correlated.
[[nodiscard]] std::uint64_t derive_seed(std::uint64_t master_seed, RngDomain domain) noexcept;

// Thin wrapper around a deterministic PRNG. Never seed this from a
// non-deterministic source (e.g. std::random_device) inside the simulation
// core -- see docs/implementation-plan.md section 5.2.
class Rng {
 public:
  explicit Rng(std::uint64_t seed) noexcept : engine_(seed) {}

  [[nodiscard]] std::uint64_t next_u64() noexcept { return engine_(); }

  [[nodiscard]] double next_uniform() noexcept {
    return std::uniform_real_distribution<double>(0.0, 1.0)(engine_);
  }

  [[nodiscard]] int next_int(int min_inclusive, int max_inclusive) noexcept {
    return std::uniform_int_distribution<int>(min_inclusive, max_inclusive)(engine_);
  }

 private:
  std::mt19937_64 engine_;
};

}  // namespace ElyverseFootball::SimCore
