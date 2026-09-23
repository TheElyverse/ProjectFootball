#include "matchSimulation.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace ElyverseFootball::SimMatch {
namespace {

using SimCore::RandomNumberGenerator;
using SimCore::RandomNumberGeneratorDomain;

// MatchRandomStreams is indexed by domain value, so it must list every domain
// in declaration order. A domain added after kAi fails this assertion instead
// of silently sharing a stream.
static_assert(static_cast<std::size_t>(RandomNumberGeneratorDomain::kAi) + 1 ==
              std::tuple_size_v<MatchRandomStreams>);

[[nodiscard]] MatchRandomStreams makeRandomStreams(const std::uint64_t seed) noexcept {
  return {
      RandomNumberGenerator(SimCore::deriveSeed(seed, RandomNumberGeneratorDomain::kExecution)),
      RandomNumberGenerator(SimCore::deriveSeed(seed, RandomNumberGeneratorDomain::kInjuries)),
      RandomNumberGenerator(SimCore::deriveSeed(seed, RandomNumberGeneratorDomain::kGeneration)),
      RandomNumberGenerator(SimCore::deriveSeed(seed, RandomNumberGeneratorDomain::kMarket)),
      RandomNumberGenerator(SimCore::deriveSeed(seed, RandomNumberGeneratorDomain::kAi)),
  };
}

[[nodiscard]] std::vector<MatchSystem> validatedSystems(std::vector<MatchSystem> systems) {
  std::set<std::string, std::less<>> names;
  for (const MatchSystem& system : systems) {
    if (system.name.empty()) {
      throw std::invalid_argument("MatchSimulation: every system needs a name");
    }
    if (!system.update) {
      throw std::invalid_argument("MatchSimulation: system '" + system.name +
                                  "' has no update function");
    }
    if (!names.insert(system.name).second) {
      throw std::invalid_argument("MatchSimulation: system name '" + system.name +
                                  "' is used twice");
    }
  }
  return systems;
}

}  // namespace

RandomNumberGenerator& MatchStepContext::random(const RandomNumberGeneratorDomain domain) const {
  return random_->at(static_cast<std::size_t>(domain));
}

MatchSimulation::MatchSimulation(MatchSimulationSpec spec)
    : systems_(validatedSystems(std::move(spec.systems))),
      clock_(SimCore::SimClock::withTicksPerSecond(spec.ticksPerSecond)),
      random_(makeRandomStreams(spec.seed)),
      previous_(spec.initialState),
      current_(spec.initialState),
      next_(std::move(spec.initialState)) {}

SimCore::SimTick MatchSimulation::step() {
  // Copy-assigning a state of the same squad reuses next_'s storage.
  next_ = current_;

  MatchStateWriter writer(next_);
  const MatchStepContext context(clock_.tick(), clock_.secondsPerTick(), random_);
  for (const MatchSystem& system : systems_) {
    system.update(context, current_, writer);
  }

  // Advance before swapping: a tick overflow throws while current_ and
  // previous_ still hold the last completed step.
  const SimCore::SimTick tick = clock_.advance();
  std::swap(previous_, current_);
  std::swap(current_, next_);
  return tick;
}

}  // namespace ElyverseFootball::SimMatch
