# Contributing to Elyverse: Football

This is the developer entry point for building, testing, and extending the project.
The game is in early development; check the relevant issue and planning documents
before starting a change.

## Contents

- [Design and architecture](#design-and-architecture)
- [Build and test](#build-and-test)
- [Run the CLI](#run-the-cli)
- [Check a change](#check-a-change)
- [Coding style](#coding-style)
  - [Language](#language)
  - [Naming](#naming)
  - [Aggregate initialization](#aggregate-initialization)
  - [Tests](#tests)
  - [Determinism](#determinism)

## Design and architecture

- [Game design document](docs/game-design-document.md): game mechanics and product
  vision (German).
- [Implementation plan](docs/implementation-plan.md): architecture, roadmap, and
  engineering requirements (German).
- [Match geometry](docs/match-geometry.md): pitch coordinates, units, and numeric
  contracts.
- [Match state](docs/match-state.md): players, ball, validation rules, and the
  seven-a-side kickoff fixture.
- [Match loop](docs/match-loop.md): the fixed-timestep simulation loop, system
  order and schedule, commands, and failure handling.
- [Player movement](docs/player-movement.md): movement targets, speed and
  acceleration limits, and the pitch boundary rule.
- [Ball movement](docs/ball-movement.md): the rolling ball, ground friction, and
  what happens when it leaves the pitch.
- [Replay format](docs/replay-format.md): the replay file `sim-cli` writes, its
  versioning and seed encoding contract, and replay playback.
- [Scenarios](docs/scenarios.md): the named, reproducible match setups `sim-cli`
  runs.

The simulation uses standard C++23 and runs independently of Unreal Engine.
Unreal will consume simulation state for presentation. Keep simulation behavior
in the core and rendering or input handling in adapters.

`sim-core` contains shared types and utilities. `sim-match` provides configurable
metric pitch geometry, the validated match state, the seven-a-side kickoff
fixture, the fixed-timestep match loop with its commands, and player and ball
movement; it depends on `sim-core`. `sim-replay` records, writes, reads and plays
back replays on top of `sim-match`. New libraries follow the existing
CMake target pattern and expose an `ElyverseFootball::<name>` alias. Unit tests live
under `tests/unit/`, grouped by module. Acceptance tests under `tests/acceptance/`
run whole scenarios and carry the CTest label `acceptance`.

## Build and test

Requirements:

- CMake 3.25 or newer.
- A C++23 compiler and standard library supporting the features used by the project,
  including `std::expected`.
- A build tool supported by the chosen CMake generator.
- Git and network access for the initial dependency download.

```sh
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
```

The first configure downloads pinned dependencies through CPM, including Catch2
for tests, [nlohmann/json 3.12.0](https://github.com/nlohmann/json/releases/tag/v3.12.0)
for replay files, and
[FTXUI 6.1.9](https://github.com/ArthurSonzogni/FTXUI/releases/tag/v6.1.9)
for the terminal interface.

Additional configure/build presets include `relwithdebinfo`, `release`, `ci`, and
`sanitize`. Use separate build directories for Windows and WSL; their CMake caches
and compiled artifacts are not interchangeable.

## Run the CLI

`sim-cli` runs a scenario headlessly, records it as a replay, and plays replays
back. After building on Linux or WSL:

```sh
./build/debug/apps/sim-cli/sim-cli --scenario kickoff --seed 42 --ticks 300 --replay-out replay.json
```

```text
scenario:     kickoff
seed:         42
ticks:        300
time:         10 s
state hash:   c22d772ab92fca0c
replay:       replay.json
```

On Windows, run `sim-cli.exe` from the CMake build output directory with the same
arguments. With Make available, `make run ARGS="..."` builds and runs it.

| Option                | Default       | Meaning                                              |
|-----------------------|---------------|------------------------------------------------------|
| `--scenario <name>`   | `kickoff`     | the scenario to run; `--list-scenarios` lists them   |
| `--seed <u64>`        | random        | the master seed; a random one is reported            |
| `--ticks <n>`         | `300`         | how many ticks to simulate, 0 to 10,000,000          |
| `--replay-out <path>` | `replay.json` | where to write the replay                            |
| `--play <path>`       |               | play a replay of at most 10,000,000 ticks back and verify its checkpoints |
| `--tui`               |               | show the result in a terminal screen                 |

A run prints the tick count, the simulated time and the final state hash, and
writes a [replay](docs/replay-format.md). `--play` rebuilds the match from the
file, verifies every recorded state hash, and prints the same summary; it takes
everything from the file, so it cannot be combined with the run options or with
`--list-scenarios`, which cannot be combined with the run options either.
`--help` wins over every other option. [Scenarios](docs/scenarios.md) describes
the scenario catalog.

Invalid arguments, an unknown scenario, a replay that cannot be read, and a
replay that does not reproduce all end with a message on stderr and a nonzero
exit code.

`--tui` requires both stdin and stdout to be terminals. The replay is written
before the screen opens; press Enter on Close, `q`, or Escape to exit.

## Check a change

Build with warnings treated as errors and run the test suite:

```sh
cmake --preset ci
cmake --build --preset ci
ctest --preset ci
```

With Make, clang-format, and run-clang-tidy available, `make ci` also runs the
repository's formatting and static-analysis checks. CI uses clang-tidy 19; see
[the workflow](.github/workflows/ci.yml) for toolchain setup.

For AddressSanitizer and UndefinedBehaviorSanitizer with GCC or Clang:

```sh
cmake --preset sanitize
cmake --build --preset sanitize
ctest --preset sanitize
```

## Coding style

`.clang-format` and `.clang-tidy` hold the mechanical rules: `make format`
applies the formatting, `make lint` reports the rest. The sections below record
the conventions behind those files, including the ones no tool can check.

### Language

Write code and comments in English, also in modules whose design documents are
German.

### Naming

Types, namespaces, and template parameters are PascalCase. Functions, methods,
variables, parameters, and public data members are camelCase. Enum values and
static class constants are kPascalCase. Private and protected data members are
camelCase with a trailing underscore. `readability-identifier-naming` is treated
as an error, so a violation fails `make lint` and CI.

### Aggregate initialization

Use designated initializers rather than positional ones:

```cpp
Vec2{.x = 3.0, .y = -4.0}  // yes
Vec2{3.0, -4.0}            // no
```

Naming each member at the point of use survives field reordering, and it makes a
swapped pair of same-typed values visible where a human writes it. This applies
to test data as much as to library code.

`modernize-use-designated-initializers` enforces the rule and is treated as an
error. It exempts `std::array` and single-member aggregates, and it skips macro
expansions, so an initializer inside an assertion macro is not covered by the
tool and needs review instead.

### Tests

Add focused tests for new behavior, invalid inputs, and relevant boundary cases.

### Determinism

Keep randomness explicit and seeded, and preserve deterministic update ordering
as simulation systems grow.
