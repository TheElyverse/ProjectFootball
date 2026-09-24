# Match loop

`MatchSimulation` advances a match in fixed time steps. It lives in `sim-match`
(`matchSimulation.hpp`) and works on the [match state](match-state.md); it
depends only on `sim-core`.

The loop owns time, update order and the rules every system follows. It does not
move anyone by itself: movement, ball physics, perception and decisions arrive as
systems that plug into it.

## One step, one tick

`step()` advances the match by exactly one tick. It takes no argument — no delta
time, no timestamp, no frame — so simulation time moves only through `step()`.
How often a caller steps changes how fast a match plays out on screen, never what
happens in it: stepping live, fast-forwarding or running headless gives the same
match.

The tick rate is a setting of `MatchSimulationSpec`, 30 Hz by default
(`kDefaultTicksPerSecond`). Elapsed time is `tick / ticksPerSecond`, one correctly
rounded division, so 300 ticks at 30 Hz are exactly 10 seconds.

| Accessor           | Meaning                                                     |
|--------------------|-------------------------------------------------------------|
| `tick()`           | ticks completed since the start                             |
| `elapsedSeconds()` | simulation time in seconds                                  |
| `state()`          | the state after the last step                               |
| `previousState()`  | the state before the last step; equal to `state()` at first |

`previousState()` and `state()` are the two snapshots a presentation layer
interpolates between to render at any frame rate.

## What happens in a step

A step from tick `t` to `t + 1`:

1. Applies the commands scheduled for tick `t`, in scheduling order.
2. Copies the resulting state into the next one.
3. Runs every system due at `t`, in the order the spec lists them. Each system
   reads the current state, commands included, and writes the next one.
4. After each system, checks the next state for non-finite values.
5. Advances the clock and makes the next state current.

Planned slot, not built yet: events are published after the clock advances.

## Commands

A command is an intent from outside the systems — a scenario script, a coach, a
test — that changes the state at a tick. `MatchCommand` is a `std::variant` of
the command types:

| Command             | Effect                                                                |
|---------------------|-----------------------------------------------------------------------|
| `MovePlayerCommand` | sets a player's movement target, moved onto the pitch if it lies off it |

`schedule(ScheduledCommand)` queues a command for the step that starts at its
tick. Commands of one tick apply in the order they were scheduled, which is the
explicit execution order a replay needs. `schedule()` rejects a tick before
`tick()`, an unknown player and a non-finite target, and leaves the queue
unchanged; a command for `tick()` itself applies in the next step. Commands
known before kickoff go into `MatchSimulationSpec::commands`, which the
constructor schedules in order and rejects with `std::invalid_argument`.

`appliedCommands()` returns every command applied so far, in application order.
Together with the initial state, seed and tick rate this is the ordered command
log of the replay contract.

Commands are applied to a copy of the current state, so a step that fails leaves
`state()` and the command log as they were.

## Systems

A `MatchSystem` has a unique `name`, an `update` function, and a schedule:

```cpp
using MatchSystemUpdate = std::function<void(
    const MatchStepContext& context, const MatchState& current, MatchStateWriter& next)>;
```

**Read current, write next.** A system never sees what another system wrote in the
same step, and a player is never updated against positions other players already
moved to in that step. Update order is fixed, so replays reproduce, but it does
not change the result: the tenth player and the first see the same pitch. Each
field should have exactly one system that writes it.

**Write positions, velocities, targets and facings only.** `MatchStateWriter`
can set player and ball positions and velocities and player targets and facings,
and nothing else.
Squad, ids, sides, attributes, player order and the pitch have no setter, so a
system cannot break those invariants. Players
are addressed by their index in `MatchState::players()`.

**Keep no state of your own.** Everything that must survive a tick belongs in the
match state. A copy of a simulation continues exactly like the original only if
systems remember nothing between ticks.

**Schedule.** A system runs in the steps whose tick satisfies
`t % intervalTicks == phaseTicks`. The default, interval 1 and phase 0, runs it
every tick. At 30 Hz:

| Rate  | Interval | Example             |
|-------|----------|---------------------|
| 30 Hz | 1        | movement            |
| 10 Hz | 3        | perception          |
| 5 Hz  | 6        | decisions           |
| 2 Hz  | 15       | tactical evaluation |

Different phases spread systems of one rate over different ticks. In a step a
system skips, the fields it writes keep their values. Rates are whole numbers of
ticks, so the schedule stays exact over a whole match.

**Randomness.** `context.random(domain)` returns the simulation's stream for a
`RandomNumberGeneratorDomain`, derived from the spec's seed with
`deriveSeed()`. Systems draw in their fixed order, so the draws are part of what
a replay reproduces.

## The standard systems

`matchSetup.hpp` assembles the systems a real match runs. `MatchConfig` holds
every tunable parameter of those systems — the tick rate and the ball physics so
far — and `makeMatchSystems(config)` returns them in their fixed order:

| Order | System          | Rate       | Writes                         |
|-------|-----------------|------------|--------------------------------|
| 1     | player movement | every tick | player positions, velocities   |
| 2     | ball movement   | every tick | ball position, velocity        |

`MatchSetup` is everything such a match starts from: initial state, config, seed
and commands. `startMatch(setup)` builds the simulation. A replay records a
setup (see [replay format](replay-format.md)); a parameter added to
`MatchConfig` must be added to the replay format too.

## Invariants and failures

The loop enforces the [match state](match-state.md) invariants every tick. The
writer makes most of them impossible to break; what remains is finiteness, which
the loop checks after every system with `findNonFiniteValues()`.

Positions off the pitch are allowed. A ball over the touchline or a player behind
the goal line is football; what it means is for a rules system to decide.

A non-finite value or an exception from a system is a defect, not a match event.
The step stops at that system and the simulation stays at the last good state:
`state()`, `previousState()` and `tick()` are unchanged. `step()` returns a
`MatchStepError` naming the tick, the system and the broken values — for an
exception, the error is recorded and the exception propagates. Every later
`step()` returns the same error without running anything, and `hasFailed()`
reports the stopped simulation.

## Snapshots and replays

A `MatchSimulation` is copyable, and a copy includes state, clock and random
streams. The copy is a complete snapshot: stepping it and the original the same
number of times gives equal states. This is what rewinding and simulating ahead
of playback build on (see the [game design document](game-design-document.md),
section 8.10).

A replay is the same `MatchSimulationSpec` built again: initial state, seed, tick
rate and systems, with the applied commands as its command list — the ordered
command log of the replay contract in the
[implementation plan](implementation-plan.md), section 5.3.

## Performance

A successful step allocates nothing: the next state reuses its storage, the three
state buffers are swapped rather than rebuilt, and the finiteness check only
allocates when it has an error to report. Steps without a command skip the
commanded copy entirely; scheduling a command may allocate, applying it does not. A 90-minute match at 30 Hz is 162,000
steps, and the world simulation will run many of them headless.

## What this is not

The loop knows nothing about match length, halves, stoppage time or the end of a
match; a rules and match-clock system will. Simulation time is never scaled: a
shorter match ends earlier, it does not run faster.
