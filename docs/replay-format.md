# Replay format

A replay file records a match so that it can be played back exactly: the replay
contract `InitialSnapshot + OrderedCommands + Seed(s) + CoreVersion` of the
[implementation plan](implementation-plan.md), section 5.3, plus the
configuration of the standard systems and state hashes to check playback against.

The `sim-replay` library (`replay.hpp`, `replayJson.hpp`) records, writes, reads
and plays back replays; it depends on `sim-match` and on
[nlohmann/json](https://github.com/nlohmann/json) for the JSON encoding. `sim-cli`
writes a replay on every successful run; `--replay-out` selects the path, the
default is `replay.json` in the working directory, and `--play` plays one back. The file is written
before the terminal interface opens, so a run rejected by argument or terminal
validation leaves no file behind.

## Schema version 2

```json
{
  "schemaVersion": 2,
  "coreVersion": "0.2.0",
  "createdAt": "2026-09-24T10:00:00Z",
  "seed": "18446744073709551615",
  "gameTime": 300,
  "config": {
    "ticksPerSecond": 30,
    "ball": { "rollingDeceleration": 1.5 }
  },
  "initialState": {
    "pitch": { "length": 60.0, "width": 40.0 },
    "playersPerSide": 7,
    "players": [
      {
        "id": 1,
        "side": "home",
        "position": { "x": 3.0, "y": 20.0 },
        "velocity": { "x": 0.0, "y": 0.0 },
        "attributes": { "maxSpeed": 7.5, "acceleration": 4.0 },
        "target": null,
        "facing": { "x": 1.0, "y": 0.0 }
      }
    ],
    "ball": {
      "position": { "x": 30.0, "y": 20.0 },
      "velocity": { "x": 6.5, "y": -1.25 }
    }
  },
  "commands": [
    { "tick": 0, "order": 0, "type": "movePlayer", "playerId": 7, "target": { "x": 40.0, "y": 10.0 } },
    { "tick": 45, "order": 0, "type": "movePlayer", "playerId": 7, "target": { "x": 10.0, "y": 35.0 } },
    { "tick": 45, "order": 1, "type": "movePlayer", "playerId": 2, "target": { "x": 30.0, "y": 20.0 } }
  ],
  "checkpoints": [
    { "tick": 0, "stateHash": "fb0d5a082f2af1ea" },
    { "tick": 30, "stateHash": "..." }
  ]
}
```

The example shortens the player list; a real file lists every player.

| Field           | JSON type | Meaning                                                       |
|-----------------|-----------|---------------------------------------------------------------|
| `schemaVersion` | number    | Version of this format. Currently `2`.                        |
| `coreVersion`   | string    | The `sim-core` version that recorded the match.               |
| `createdAt`     | string    | Creation time in UTC, `%Y-%m-%dT%H:%M:%SZ`. Metadata only.    |
| `seed`          | string    | Unsigned 64-bit master seed, in decimal.                      |
| `gameTime`      | number    | The tick the match was recorded up to.                        |
| `config`        | object    | Parameters of the standard systems (`MatchConfig`).           |
| `initialState`  | object    | The match state at tick 0, every field of `MatchState`.       |
| `commands`      | array     | Every applied command, in execution order.                    |
| `checkpoints`   | array     | State hashes after the steps that reached these ticks.        |

Positions are meters and velocities meters per second, as in the
[match state](match-state.md). A player's `target` is `null` when he has none.

### Commands and their order

Commands are listed in execution order: by `tick`, and within a tick by `order`,
which counts `0, 1, 2, …`. Both are explicit, and a reader rejects a file whose
array order disagrees with them instead of re-sorting it: a silently re-sorted
command log is a different match. Commands of one tick execute in the order they
were scheduled (see [match loop](match-loop.md), section *Commands*).

| `type`       | Fields                 | Command             |
|--------------|------------------------|---------------------|
| `movePlayer` | `playerId`, `target`   | `MovePlayerCommand` |

A recorded replay holds the commands the simulation applied. A command scheduled
during the run is included; one scheduled for a tick the match never reached is
not.

### Checkpoints

`stateHash` is `hashMatchState()` of the state after the step that reached `tick`,
as 16 lowercase hexadecimal digits: a stable FNV-1a hash over every field of the
state (`matchStateHash.hpp`). The recorder takes one of the initial state (tick
0), one every 30 ticks by default, and one of the final state. Checkpoints are
ascending, unique, and never past `gameTime`; the first is at tick 0 and the last
at `gameTime`, so playback always compares the initial and the final state. Every
command runs before `gameTime`: a command at or after it would never be applied
and is not part of the recorded match.

## Playing a replay back

`playReplay()` rebuilds the match from `initialState`, `config`, `seed` and
`commands` with the standard systems, runs it to `gameTime`, and compares the
state hash at every checkpoint. It reports the first mismatch with the tick and
both hashes. Recording, saving, loading and playing back reproduce every
checkpoint exactly; the unit tests in `tests/unit/sim-replay` hold that.

## Rejected files

Every rejection carries a `ReplayErrorCode` and a message naming the offending
field by its path, for example `initialState.players[3].position.x: expected a
number`.

| Code                        | Cause                                                                  |
|-----------------------------|------------------------------------------------------------------------|
| `kIoError`                  | the file cannot be read or written                                     |
| `kMalformed`                | not JSON, a missing or mistyped field, commands out of order           |
| `kUnsupportedSchemaVersion` | a `schemaVersion` other than 2                                         |
| `kIncompatibleCoreVersion`  | recorded with another `coreVersion`                                    |
| `kInvalidSetup`             | an invalid initial state, command or checkpoint list                   |
| `kSimulationFailed`         | a step of the playback failed                                          |
| `kCheckpointMismatch`       | playback does not reproduce a recorded hash                            |

A replay is only valid for the core version that recorded it. A change to
simulation behavior bumps `coreVersion` and makes older replays unplayable by
design, instead of letting them play back into a different match.

## The seed is a string on purpose

`seed` is an unsigned 64-bit value serialized as a decimal JSON string, for
example `"18446744073709551615"` for `UINT64_MAX`. The quotes are not cosmetic.
JSON numbers carry no integer guarantee, and many parsers decode them as IEEE 754
doubles, which represent integers exactly only up to 2^53. A larger seed would
come back silently rounded, and a rounded seed is a different simulation. State
hashes are strings for the same reason.

Consumers must preserve the string or parse it as an unsigned 64-bit integer,
never through a floating-point type. Producers must keep the quotes: removing
them looks like a cleanup and invalidates every recorded replay.

`tests/cli/smoke.cmake` guards this. It asserts that the JSON type of `seed` is a
string, and feeds `UINT64_MAX` back through the CLI to verify the round trip.

Doubles are written in their shortest round-trippable form and read back bit for
bit, so an initial state survives the file unchanged.

## Versioning

`schemaVersion` describes the format of this file. It is a single integer, not a
semver string, and it counts up by one.

Raise it when a reader written against this document would misread a new file:

- a field is removed or renamed,
- a field changes its JSON type,
- a field changes its meaning, unit, or encoding.

Leave it unchanged otherwise. Key order, indentation and line endings are not
part of the contract, and no consumer may depend on them. A field that the
simulation needs to reproduce a match, such as a new configuration parameter or
state field, is required: a reader cannot guess its value, so such an addition
comes with a new `coreVersion`.

Consumers read `schemaVersion` first and reject a version they do not know, with
an error naming the version they found. Guessing at an unfamiliar layout is worse
than refusing it, because a misread seed yields a run that looks valid and
reproduces nothing.

Producers write exactly one version, the current one. There is no negotiation and
no writing of older layouts. A change that raises the version updates this
document, `replayJson.cpp`, and its tests in the same commit, and records here
whether files of the previous version stay readable, and by what.

`schemaVersion` and `coreVersion` are independent. The first describes the shape
of the file, the second the simulation that produced it. A change to simulation
behavior -- a different random mapping, a different update order -- invalidates
recorded replays without changing the file format, and surfaces as a new
`coreVersion` value rather than a new `schemaVersion`.

| Version | Change                                                                                     |
|---------|--------------------------------------------------------------------------------------------|
| 1       | Initial schema: metadata only (`coreVersion`, `createdAt`, `seed`, `gameTime`).            |
| 2       | A playable replay: adds `config`, `initialState`, `commands` and `checkpoints`; `gameTime` is the final tick. Version 1 files cannot be played back and are rejected. |
