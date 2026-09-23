#pragma once

#include <array>
#include <cstdint>
#include <expected>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "matchState.hpp"
#include "random.hpp"
#include "simTime.hpp"

namespace ElyverseFootball::SimMatch {

// The match simulation's tick rate unless a spec states another. 30 Hz is the
// movement rate of docs/implementation-plan.md section 6.3; it is a setting,
// so a faster ball model can raise it without changing the loop.
inline constexpr int kDefaultTicksPerSecond = 30;

// One independent stream per RandomNumberGeneratorDomain, indexed by the
// domain's value. Part of the simulation, so a copy of a simulation continues
// with the same draws as the original.
using MatchRandomStreams = std::array<SimCore::RandomNumberGenerator, 5>;

// What a system learns about the step it runs in. tick() is the tick being
// simulated: a step from tick t to t + 1 reports t.
class MatchStepContext {
 public:
  [[nodiscard]] SimCore::SimTick tick() const noexcept { return tick_; }
  [[nodiscard]] double secondsPerTick() const noexcept { return secondsPerTick_; }

  // The simulation's stream for a domain. Systems draw in their fixed update
  // order, so the draws are part of what a replay reproduces.
  [[nodiscard]] SimCore::RandomNumberGenerator& random(
      SimCore::RandomNumberGeneratorDomain domain) const;

 private:
  friend class MatchSimulation;

  MatchStepContext(const SimCore::SimTick tick, const double secondsPerTick,
                   MatchRandomStreams& random) noexcept
      : tick_(tick), secondsPerTick_(secondsPerTick), random_(&random) {}

  SimCore::SimTick tick_;
  double secondsPerTick_;
  MatchRandomStreams* random_;
};

// A system reads the current tick's state and writes the next one. It never
// sees what another system wrote in the same step, so the result of a step
// does not depend on which player or which system came first -- the order is
// fixed for determinism, and irrelevant for fairness.
//
// Systems keep no state of their own: everything that must survive a tick
// belongs in the match state. A copy of a simulation shares its systems'
// behavior, and a system that remembered something between ticks would make
// the copy diverge from the original.
using MatchSystemUpdate = std::function<void(const MatchStepContext& context,
                                             const MatchState& current, MatchStateWriter& next)>;

// A system runs in the steps whose tick t satisfies
// t % intervalTicks == phaseTicks. The default runs it every tick. At 30 Hz an
// interval of 3 is a 10 Hz system, such as perception; different phases
// spread systems of the same rate over different ticks. In a step a system
// skips, the fields it writes keep their current values.
struct MatchSystem {
  // Unique within a simulation; names the system in diagnostics.
  std::string name;
  MatchSystemUpdate update;
  int intervalTicks = 1;
  int phaseTicks = 0;
};

// Why a step failed. A failed step is a defect in a system, not a match event:
// the simulation keeps the last good state for diagnosis and runs no further
// steps.
struct MatchStepError {
  // The tick the failed step started from.
  SimCore::SimTick tick;
  // The system that wrote a non-finite value or threw.
  std::string systemName;
  // What the system broke; empty if it threw instead.
  std::vector<MatchStateError> errors;

  friend bool operator==(const MatchStepError&, const MatchStepError&) = default;
};

// Everything a simulation starts from. Replaying a match means building the
// same spec again: the same initial state, seed, tick rate and systems.
struct MatchSimulationSpec {
  MatchState initialState;
  std::uint64_t seed = 0;
  int ticksPerSecond = kDefaultTicksPerSecond;
  // Update order: systems run in this order in every step.
  std::vector<MatchSystem> systems;
};

// The fixed-timestep match loop. step() advances the match by exactly one tick
// and takes no time input: simulation time moves only through step(), never
// through wall-clock time or a frame rate, so how fast a caller steps -- live,
// fast-forward, or headless -- cannot change the result.
//
// A step copies the current state into the next one, runs every system due in
// that tick in order against (current, next), then makes next current. The state before the
// last step stays available as previousState(), which is what a presentation
// layer interpolates between.
//
// After each system the next state is checked for non-finite positions and
// velocities; other invariants cannot break, because MatchStateWriter has no
// way to change them. A step that fails leaves state(), previousState() and
// tick() as they were before it, and every later step() reports the same
// failure without running anything.
//
// Copying a MatchSimulation copies state, clock and random streams: the copy
// is a complete snapshot that continues exactly like the original.
class MatchSimulation {
 public:
  // Throws std::invalid_argument for a tick rate below 1, a system without a
  // name or update function, two systems with the same name, an interval
  // below 1, or a phase outside [0, interval).
  explicit MatchSimulation(MatchSimulationSpec spec);

  // Advances exactly one tick and returns the new tick. Returns the error of
  // the first failed step once a step has failed. A system that throws marks
  // the simulation failed and the exception propagates.
  [[nodiscard]] std::expected<SimCore::SimTick, MatchStepError> step();

  [[nodiscard]] bool hasFailed() const noexcept { return failure_.has_value(); }

  [[nodiscard]] SimCore::SimTick tick() const noexcept { return clock_.tick(); }
  [[nodiscard]] double elapsedSeconds() const noexcept { return clock_.elapsedSeconds(); }
  [[nodiscard]] int ticksPerSecond() const noexcept { return clock_.ticksPerSecond(); }

  [[nodiscard]] const MatchState& state() const noexcept { return current_; }

  // The state one tick before state(); equal to it before the first step.
  [[nodiscard]] const MatchState& previousState() const noexcept { return previous_; }

 private:
  std::vector<MatchSystem> systems_;
  SimCore::SimClock clock_;
  MatchRandomStreams random_;
  MatchState previous_;
  MatchState current_;
  // Scratch buffer the systems write into; holds no meaningful state between
  // steps. Kept as a member so a step reuses its storage instead of
  // allocating.
  MatchState next_;
  std::optional<MatchStepError> failure_;
};

}  // namespace ElyverseFootball::SimMatch
