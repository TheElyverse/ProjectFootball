# Elyverse: Football

_Living Football_

Build with CMake 3.25+ and a C++23 compiler:

```sh
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
```

The first configure downloads dependencies through CPM, including
[FTXUI 6.1.9](https://github.com/ArthurSonzogni/FTXUI/releases/tag/v6.1.9)
for the command line interface.

Run the terminal interface from an interactive terminal:

```sh
make run ARGS="--tui --seed 42 --replay-out replay_metadata.json"
```

The screen displays the empty simulation's core version, seed, game time, and saved
replay metadata path. Press Enter on Close, `q`, or Escape to exit. Replay metadata
is written before the screen opens. The simulation currently has no domain state.

Replay metadata schema version 1 stores `seed` as an unsigned decimal JSON string
(for example, `"18446744073709551615"` for `UINT64_MAX`). Consumers must preserve
the string or parse it as an unsigned 64-bit integer, without conversion through
floating point.

Omit `--tui` for the existing one-shot command, suitable for scripts and redirected
output. `--tui` requires both stdin and stdout to be terminals. On Windows, run
`sim-cli.exe` from the CMake build output directory with the same arguments.
