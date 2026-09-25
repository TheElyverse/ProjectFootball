# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project status

This repository is in **early bootstrap stage**. The P0 Foundation milestone (repo, CMake, CI, strong
IDs, sim clock, deterministic RNG, a minimal event bus, and a CLI) exists under `libs/sim-core` and
`apps/sim-cli`, and `libs/sim-tactics` holds the tactic data model and its file format (`docs/tactics.md`,
`docs/tactic-format.md`, files under `data/tactics/`), and `libs/sim-match` has started with pitch geometry, the validated match state, the
seven-a-side kickoff fixture, the fixed-timestep match loop with commands (`docs/match-loop.md`), and
player and ball movement (`docs/player-movement.md`, `docs/ball-movement.md`), spatial queries,
perception, possession, passing, reception and pass decisions (`docs/spatial-queries.md`,
`docs/perception.md`, `docs/possession.md`, `docs/passing.md`, `docs/reception.md`,
`docs/pass-candidates.md`, `docs/pass-decisions.md`), tactical phases, pitch control, zones and team shape, desired regions, off-ball movement, the defensive block and pressing (`docs/match-phases.md`,
`docs/pitch-control.md`, `docs/zones.md`, `docs/desired-region.md`, `docs/off-ball-movement.md`, `docs/defensive-shape.md`,
`docs/pressing.md`), tactic changes by command and simplified restarts (`docs/match-loop.md`, `docs/restarts.md`)
with events and diagnostics (`docs/match-events.md`, `docs/decision-trace.md`); `libs/sim-analytics` computes
match statistics from events (`docs/match-analytics.md`); `apps/sim-benchmark` plays series and round robins of
tactic matches (`docs/sim-benchmark.md`); a web debug viewer with tactical overlays lives in `apps/sim-viewer`
(`docs/debug-viewer.md`); the P1 passing scenarios, the P2 tactical identities and golden scenarios come with
their acceptance tests (`docs/scenarios.md`, `docs/tactical-identities.md`, `docs/golden-scenarios.md`);
almost everything described in the design/implementation docs below is still unbuilt.
When implementing a new system, check whether it belongs in an existing module (see layout below) before
adding a new one.

The two authoritative planning docs are:
- `docs/game-design-document.md` — product/design vision (German). Defines *what* the game is.
- `docs/implementation-plan.md` — technical architecture, roadmap, and engineering plan (German prose,
  English technical terms/code). Defines *how* it will be built.

Both documents are fairly long; read the relevant section rather than the whole file when possible. They
are the source of truth for scope and architecture decisions until real code exists — prefer them over
assumptions.

## Intended architecture (from the implementation plan)

The project ("Elyverse: Football") is planned as a **deterministic, headless C++ simulation core**
(Unreal Engine 5 is only the presentation/adapter layer, never the source of truth for simulation logic).
Key architectural rules to preserve once code exists:

- **Dependency direction**: `World/Match Simulation Core → Application Layer (Commands/Queries/Events) →
  Adapters (CLI, Persistence, Unreal)`. Inner layers must never depend on outer ones — `sim-match` must
  never know about Unreal types or UObject lifecycle.
- **Same systems for player and AI**: there is no separate "AI shortcut" path for core workflows (e.g.
  recruitment, delegation). Player-driven and AI-driven actions go through the same Commands.
- **Determinism**: same initial state + commands + seed must always produce the same result. RNG must use
  explicit per-domain streams, never global non-deterministic sources. Replays are defined as
  `InitialSnapshot + OrderedCommands + Seed(s) + CoreVersion`.
- **Decision/execution separation**: throughout the simulation (player decisions, ball actions, etc.), a
  "what does the agent decide" step is modeled separately from "how well is it executed" — this pipeline
  (`Perceive → Generate candidates → Estimate utility → Select probabilistically → Execute technically →
  Observe consequences`) recurs in multiple systems and should not be collapsed into a single roll.

### Module layout

```
libs/
  sim-core       IDs, time, RNG, events, base types (depends on: STL only)      [exists]
  sim-player     Capabilities, match/world player state, development           [planned]
  sim-tactics    Principles, phases, responsibilities, spatial targets         [exists: tactic model]
  sim-match      Pitch, ball, perception, decisions, actions, rules            [exists: pitch, state, loop, movement, ball, perception, possession, passing, reception, decisions, events, phases, pitch control, zones, desired region, off-ball, defence, pressing, tactic changes, restarts, scenarios]
  sim-world      Calendar, clubs, competitions, economy, careers               [planned]
  sim-ai         Club planning, coach decisions, staff behavior                [planned]
  sim-analytics  Events, metrics, explanations (read-only over domain events)  [exists: match stats, series summaries]
  sim-replay     Replay recording, JSON file format, playback verification,
                 debug frames for the viewer, decision traces                  [exists]
apps/
  sim-cli        runs scenarios, records and plays back replays               [exists]
  sim-viewer     TypeScript/Canvas debug viewer for sim-cli's frames (npm)    [exists]
  website        static landing page, HTML + Tailwind CSS v4 (npm)          [exists]
  sim-benchmark  series and style round robins of tactic matches, statistics [exists]
  sim-replay, unreal-game                                                      [planned]
data/            schemas, tactics, competitions, fixtures (JSON/YAML, schema-validated) [exists: tactics]
tests/unit/      Catch2 tests, mirrors libs/ by subdirectory                   [exists: sim-core, sim-tactics, sim-match, sim-replay, sim-analytics, sim-benchmark]
tests/acceptance/ whole-match scenarios: stability, determinism, pinned hashes [exists: M0, P1, P2]
tests/benchmark/ sim-benchmark smoke test and the Release-only P2 style benchmark [exists]
```

Stack in use: C++23, CMake + Ninja presets, CPM.cmake for dependencies (see `cmake/get_cpm.cmake`),
Catch2 v3 for tests, GitHub Actions for CI (`.github/workflows/ci.yml`, Linux + Windows). New libs follow
the same pattern as `libs/sim-core`: a `CMakeLists.txt` building a static lib aliased as `ElyverseFootball::<name>`,
public headers directly under `libs/<name>/include/`, and a matching `tests/unit/<name>/` directory
added to `tests/unit/CMakeLists.txt`. Export that `include/` directory with
`target_include_directories(... PUBLIC ...)`; consumers include headers by filename, for example
`#include "simTime.hpp"` for `libs/sim-core/include/simTime.hpp`.

### Testing strategy (planned)

Beyond ordinary unit tests, the plan calls for: **replay tests** (deterministic tick/event hashing),
**tactical tests** (statistically distinguishing playstyles like possession/counter/pressing), and
**statistical guardrail tests** (distributions over 100k+ simulated runs, checked against confidence bands
rather than exact values). Keep this in mind when scaffolding the test suite — plain pass/fail unit tests
are not sufficient for the match simulation core.

## Common commands

Configure + build (Debug preset):
```
cmake --preset debug
cmake --build --preset debug
```

Run all tests:
```
ctest --preset debug --output-on-failure
```

Run a single test (Catch2 tag or exact name), after building:
```
./build/debug/tests/unit/sim-core-tests "[rng]"
./build/debug/tests/unit/sim-core-tests "RandomNumberGenerator is deterministic for a given seed"
./build/debug/tests/unit/sim-match-tests "[matchState]"
./build/debug/tests/unit/sim-match-tests "[matchSimulation]"
```

Run a scenario headlessly and play its replay back (see `docs/replay-format.md`, `docs/scenarios.md`):
```
./build/debug/apps/sim-cli/sim-cli --scenario kickoff --seed 42 --ticks 300 --replay-out /tmp/replay.json
./build/debug/apps/sim-cli/sim-cli --play /tmp/replay.json
```

Record debug frames and watch them in the web viewer (Node.js 22+, see `docs/debug-viewer.md`):
```
./build/debug/apps/sim-cli/sim-cli --scenario m0-acceptance --seed 42 --frames-out frames.json
cd apps/sim-viewer && npm install && npm run build && npm run serve -- ../../frames.json
cd apps/sim-viewer && npm test
```

Build and preview the static website (see `docs/website.md`):
```
cd apps/website && npm install && npm run build && npm run serve
```

Run only the acceptance scenarios (CI runs them in their own step):
```
ctest --preset debug --label-regex acceptance
```

Play two tactic files against each other, write match statistics, and trace a player's decisions in the replay
(see `docs/tactical-identities.md`, `docs/match-analytics.md`, `docs/decision-trace.md`):
```
./build/debug/apps/sim-cli/sim-cli --home-tactic data/tactics/pressing.json --away-tactic data/tactics/counter.json \
  --seed 3 --ticks 1800 --replay-out /tmp/replay.json --stats-out /tmp/stats.json
./build/debug/apps/sim-cli/sim-cli --play /tmp/replay.json --trace /tmp/trace.txt --trace-players 4
```

Benchmark the tactical styles in a Release build (see `docs/sim-benchmark.md`); CI runs the reduced P2 round robin
in its `style-benchmark` job:
```
cmake --preset release && cmake --build --preset release --target sim-benchmark
./build/release/apps/sim-benchmark/sim-benchmark --style data/tactics/possession.json \
  --style data/tactics/counter.json --style data/tactics/pressing.json --matches 20 --minutes 90 --jobs 2
cmake --preset ci-release && cmake --build --preset ci-release --target sim-benchmark
ctest --preset ci-release --label-regex benchmark
```

Run the complete local CI target (warnings-as-errors build, tests, format check, and clang-tidy):

```
make ci
```

Sanitizer build (ASan+UBSan, GCC/Clang only, not MSVC):
```
cmake --preset sanitize && cmake --build --preset sanitize && ctest --preset sanitize
```

Auto-format:
```
find libs apps tests -name '*.hpp' -o -name '*.cpp' | xargs clang-format -i
```

## Commit messages

A commit message is a short imperative subject, an optional body explaining why, and the
issue number as its only trailer:

```
Add validated match state

Introduce MatchState with two teams, their players and one ball.

#3
```

Never add attribution trailers such as `Co-Authored-By:` or `Claude-Session:`, in commit
messages or in pull request descriptions.

## Language note

Design/planning docs are written primarily in German with English technical terms. Match that convention
if extending those specific documents. Code (identifiers, comments) is English.
