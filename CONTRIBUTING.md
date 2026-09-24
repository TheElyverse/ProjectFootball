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
  order and schedule, and failure handling.
- [Replay metadata](docs/replay-metadata.md): the file `sim-cli` writes and its
  seed encoding contract.

The simulation uses standard C++23 and runs independently of Unreal Engine.
Unreal will consume simulation state for presentation. Keep simulation behavior
in the core and rendering or input handling in adapters.

`sim-core` contains shared types and utilities. `sim-match` provides configurable
metric pitch geometry, the validated match state, the seven-a-side kickoff
fixture, and the fixed-timestep match loop; it depends on `sim-core`. New libraries follow the existing
CMake target pattern and expose an `ElyverseFootball::<name>` alias. Unit tests live
under `tests/unit/`, grouped by module.

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
for tests and [FTXUI 6.1.9](https://github.com/ArthurSonzogni/FTXUI/releases/tag/v6.1.9)
for the terminal interface.

Additional configure/build presets include `relwithdebinfo`, `release`, `ci`, and
`sanitize`. Use separate build directories for Windows and WSL; their CMake caches
and compiled artifacts are not interchangeable.

## Run the CLI

From an interactive terminal, with Make available:

```sh
make run ARGS="--tui --seed 42 --replay-out replay_metadata.json"
```

Alternatively, after building on Linux or WSL:

```sh
./build/debug/apps/sim-cli/sim-cli --tui --seed 42 --replay-out replay_metadata.json
```

On Windows, run `sim-cli.exe` from the CMake build output directory with the same
arguments.

The CLI currently starts an empty simulation and writes replay metadata. It does
not yet run a match. The terminal screen displays the core version, seed, game
time, and metadata path. Press Enter on Close, `q`, or Escape to exit. Metadata is
written before the screen opens; [replay metadata](docs/replay-metadata.md)
describes the file's schema.

Omit `--tui` for one-shot execution suitable for scripts and redirected output.
`--tui` requires both stdin and stdout to be terminals.

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
