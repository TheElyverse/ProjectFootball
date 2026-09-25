#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "ids.hpp"
#include "matchCommand.hpp"
#include "matchEvents.hpp"
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

// Which decisions to explain when diagnostics are on: those of some players,
// in a range of ticks. The default explains every decision. A narrow filter
// keeps the cost of diagnostics to what a debugging session looks at; the
// match itself is the same with any filter.
struct DiagnosticsFilter {
  // The players to explain; empty explains everyone.
  std::vector<SimCore::PlayerId> players;
  // The first and last tick of the steps to explain, both inclusive; empty
  // leaves that end open.
  std::optional<SimCore::SimTick> from;
  std::optional<SimCore::SimTick> to;

  [[nodiscard]] bool includesTick(SimCore::SimTick tick) const noexcept;
  [[nodiscard]] bool includesPlayer(SimCore::PlayerId player) const noexcept;

  friend bool operator==(const DiagnosticsFilter&, const DiagnosticsFilter&) = default;
};

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

  // Records a domain event of this step. Events are part of the match: a
  // replay reproduces them in the order systems record them.
  void record(const MatchEvent& event) const;

  // Whether anyone asked for diagnostics of this step. A system may skip
  // building them when not; it must decide the same either way.
  [[nodiscard]] bool collectsDiagnostics() const noexcept {
    return diagnostics_.decisions != nullptr;
  }
  // Whether anyone asked for diagnostics of this player's decisions in this
  // step: collectsDiagnostics() and the filter includes him.
  [[nodiscard]] bool collectsDiagnostics(SimCore::PlayerId player) const noexcept {
    return collectsDiagnostics() && diagnostics_.filter->includesPlayer(player);
  }

  // Keeps a diagnostic of this step if diagnostics of its player are
  // collected, and drops it otherwise.
  void diagnose(DecisionDiagnostic diagnostic) const;
  void diagnose(ActionDiagnostic diagnostic) const;

 private:
  friend class MatchSimulation;

  // The diagnostics a step collects, if any are collected.
  struct Diagnostics {
    std::vector<DecisionDiagnostic>* decisions = nullptr;
    std::vector<ActionDiagnostic>* actions = nullptr;
    const DiagnosticsFilter* filter = nullptr;
  };

  MatchStepContext(const SimCore::SimTick tick, const double secondsPerTick,
                   MatchRandomStreams& random, std::vector<MatchEvent>& events,
                   const Diagnostics diagnostics) noexcept
      : tick_(tick),
        secondsPerTick_(secondsPerTick),
        random_(&random),
        events_(&events),
        diagnostics_(diagnostics) {}

  SimCore::SimTick tick_;
  double secondsPerTick_;
  MatchRandomStreams* random_;
  std::vector<MatchEvent>* events_;
  Diagnostics diagnostics_;
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
// same spec again: the same initial state, seed, tick rate, systems and
// commands.
struct MatchSimulationSpec {
  MatchState initialState;
  std::uint64_t seed = 0;
  int ticksPerSecond = kDefaultTicksPerSecond;
  // Update order: systems run in this order in every step.
  std::vector<MatchSystem> systems;
  // Commands known before kickoff, such as a scenario script. Scheduled in
  // this order, as if by schedule() before the first step.
  std::vector<ScheduledCommand> commands;
};

// The fixed-timestep match loop. step() advances the match by exactly one tick
// and takes no time input: simulation time moves only through step(), never
// through wall-clock time or a frame rate, so how fast a caller steps -- live,
// fast-forward, or headless -- cannot change the result.
//
// A step first applies the commands scheduled for its tick, then copies the
// current state into the next one, runs every system due in that tick in order
// against (current, next), and makes next current. The state before the
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
  // below 1, a phase outside [0, interval), or a command schedule() would
  // reject.
  explicit MatchSimulation(MatchSimulationSpec spec);

  // Queues a command for the step that starts at command.tick, after every
  // command already queued for that tick. Rejects a tick before tick() -- the
  // step from tick() is the earliest one that can still apply it -- an
  // unknown player, and a non-finite target, leaving the queue unchanged.
  [[nodiscard]] std::expected<void, MatchCommandError> schedule(const ScheduledCommand& command);

  // Every command applied so far, in the order it was applied. Together with
  // the spec's initial state, seed and tick rate this is the ordered command
  // log of a replay.
  [[nodiscard]] std::span<const ScheduledCommand> appliedCommands() const noexcept;

  // Advances exactly one tick and returns the new tick. Returns the error of
  // the first failed step once a step has failed. A system that throws marks
  // the simulation failed and the exception propagates.
  [[nodiscard]] std::expected<SimCore::SimTick, MatchStepError> step();

  [[nodiscard]] bool hasFailed() const noexcept { return failure_.has_value(); }

  // The events of the last successful step, in the order systems recorded
  // them; empty before the first step. A command that changes possession
  // records its event before the systems run.
  [[nodiscard]] std::span<const MatchEvent> events() const noexcept { return events_; }

  // Turns decision diagnostics on or off for the following steps; off by
  // default. Diagnostics are read-only output: collecting them changes
  // nothing in the match and draws no random numbers.
  void setCollectDiagnostics(bool collect) noexcept { collectDiagnostics_ = collect; }

  // Restricts collected diagnostics to some players and ticks; see
  // DiagnosticsFilter. Takes effect with the next step.
  void setDiagnosticsFilter(DiagnosticsFilter filter) { diagnosticsFilter_ = std::move(filter); }

  // The decision diagnostics of the last successful step, if collected.
  [[nodiscard]] std::span<const DecisionDiagnostic> diagnostics() const noexcept {
    return diagnostics_;
  }

  // The action diagnostics of players without the ball in the last
  // successful step, if collected.
  [[nodiscard]] std::span<const ActionDiagnostic> actionDiagnostics() const noexcept {
    return actionDiagnostics_;
  }

  [[nodiscard]] SimCore::SimTick tick() const noexcept { return clock_.tick(); }
  [[nodiscard]] double elapsedSeconds() const noexcept { return clock_.elapsedSeconds(); }
  [[nodiscard]] int ticksPerSecond() const noexcept { return clock_.ticksPerSecond(); }

  [[nodiscard]] const MatchState& state() const noexcept { return current_; }

  // The state one tick before state(); equal to it before the first step.
  [[nodiscard]] const MatchState& previousState() const noexcept { return previous_; }

 private:
  // Applies the commands due at the current tick to commanded_ and returns
  // the state systems read in this step: commanded_ if any command was due,
  // current_ otherwise.
  [[nodiscard]] const MatchState& applyDueCommands();

  std::vector<MatchSystem> systems_;
  // Applied commands first, then queued ones, ordered by tick and, within a
  // tick, by scheduling order. Everything before appliedCount_ has been
  // applied; keeping both in one vector makes the log free.
  std::vector<ScheduledCommand> commands_;
  std::size_t appliedCount_ = 0;
  SimCore::SimClock clock_;
  MatchRandomStreams random_;
  MatchState previous_;
  MatchState current_;
  // Scratch buffer holding current_ with this step's commands applied, so a
  // failed step leaves current_ untouched.
  MatchState commanded_;
  // Scratch buffer the systems write into; holds no meaningful state between
  // steps. Kept as a member so a step reuses its storage instead of
  // allocating.
  MatchState next_;
  std::optional<MatchStepError> failure_;
  // Scratch buffers for the step in progress, swapped in on success so a
  // failed step leaves the last step's output untouched.
  std::vector<MatchEvent> stepEvents_;
  std::vector<DecisionDiagnostic> stepDiagnostics_;
  std::vector<ActionDiagnostic> stepActionDiagnostics_;
  std::vector<MatchEvent> events_;
  std::vector<DecisionDiagnostic> diagnostics_;
  std::vector<ActionDiagnostic> actionDiagnostics_;
  bool collectDiagnostics_ = false;
  DiagnosticsFilter diagnosticsFilter_;
};

}  // namespace ElyverseFootball::SimMatch
