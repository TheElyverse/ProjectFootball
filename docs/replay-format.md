# Replay format

A replay file records a match so that it can be played back exactly: the replay
contract `InitialSnapshot + OrderedCommands + Seed(s) + CoreVersion` of the
[implementation plan](implementation-plan.md), section 5.3, plus the
configuration of the standard systems and state and event hashes to check
playback against.

The `sim-replay` library (`replay.hpp`, `replayJson.hpp`) records, writes, reads
and plays back replays; it depends on `sim-match` and on
[nlohmann/json](https://github.com/nlohmann/json) for the JSON encoding. `sim-cli`
writes a replay on every successful run; `--replay-out` selects the path, the
default is `replay.json` in the working directory, and `--play` plays one back. The file is written
before the terminal interface opens, so a run rejected by argument or terminal
validation leaves no file behind.

## Schema version 3

```json
{
  "schemaVersion": 3,
  "coreVersion": "0.13.0",
  "createdAt": "2026-09-24T10:00:00Z",
  "seed": "18446744073709551615",
  "gameTime": 300,
  "config": {
    "ticksPerSecond": 30,
    "ball": { "rollingDeceleration": 1.5, "carryDistance": 0.5 },
    "perception": {
      "intervalTicks": 3,
      "viewDistance": 60.0,
      "fieldOfViewDegrees": 180.0,
      "awarenessRadius": 3.0,
      "memorySeconds": 3.0,
            "extrapolationSeconds": 1.0
    },
    "passing": {
      "arrivalSpeed": 4.0,
      "maxSpeed": 22.0,
            "directionError": 0.03,
            "speedError": 0.05,
      "pressureRadius": 3.0,
      "pressureErrorFactor": 1.0
    },
    "reception": { "controlRadius": 1.0, "reclaimDelaySeconds": 0.3 },
        "pursuit": { "intervalTicks": 3, "sampleSeconds": 0.1, "horizonSeconds": 8.0 },
    "decisions": {
      "intervalTicks": 6,
      "minHoldSeconds": 0.5,
      "temperature": 0.15,
      "scoring": {
        "minConfidence": 0.3,
        "minPassDistance": 2.0,
        "maxPassDistance": 35.0,
        "interceptionMarginSeconds": 0.6,
        "pressureRadius": 6.0,
        "minCompletion": 0.35,
        "completionWeight": 1.0,
        "progressionWeight": 0.8,
        "pressureWeight": 0.3,
                "riskWeight": 0.3
      }
    },
        "phases": { "intervalTicks": 10, "transitionSeconds": 4.0, "hysteresisMeters": 3.0 },
        "pitchControl": { "intervalTicks": 10, "cellSize": 2.0, "controlSeconds": 0.5 },
    "positioning": {
      "intervalTicks": 6,
      "candidateSpacing": 3.0,
      "targetDistanceScale": 10.0,
      "spacingRadius": 6.0,
      "pressureRadius": 8.0,
      "minConfidence": 0.3,
      "hysteresisCost": 0.15,
            "maxShiftMeters": 3.0
    },
    "offBall": {
      "nearBallRadius": 20.0,
      "nearIntervalTicks": 6,
      "farIntervalTicks": 18,
      "supportDistance": 10.0,
      "spaceSearchRadius": 12.0,
      "runDepth": 6.0,
      "laneRadius": 4.0,
      "pressedRadius": 6.0,
      "effortScale": 20.0,
      "holdResponsibility": 0.4,
      "temperature": 0.2,
      "responsibilityWeight": 1.0,
      "regionWeight": 0.5,
      "spaceWeight": 0.6,
      "laneWeight": 0.8,
      "urgencyWeight": 0.6,
            "effortWeight": 0.3
    },
    "defensive": {
      "markRadius": 12.0,
      "markDistance": 1.5,
      "trackRadius": 15.0,
      "runnerSpeed": 3.0,
      "trackLeadSeconds": 0.5,
            "coverDistance": 6.0,
      "pressRadius": 15.0,
      "pressDistance": 1.0,
      "laneMinDistance": 2.0,
      "effortScale": 20.0,
      "holdResponsibility": 0.4,
      "temperature": 0.2,
      "responsibilityWeight": 1.0,
      "regionWeight": 0.5,
      "spaceWeight": 0.4,
            "urgencyWeight": 0.8,
      "effortWeight": 0.3
    },
    "challenge": {
      "intervalTicks": 3,
      "radius": 1.2,
      "winChance": 0.2,
      "attemptSeconds": 0.5,
      "protectSeconds": 0.5
    }
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
      "velocity": { "x": 6.5, "y": -1.25 },
            "owner": null,
      "lastTouch": null
    },
    "tactics": { "home": { "format": "elyverse-tactic", "version": 1, "name": "reference" }, "away": null }
  },
  "commands": [
    { "tick": 0, "order": 0, "type": "movePlayer", "playerId": 7, "target": { "x": 40.0, "y": 10.0 } },
    { "tick": 45, "order": 0, "type": "movePlayer", "playerId": 7, "target": { "x": 10.0, "y": 35.0 } },
    { "tick": 45, "order": 1, "type": "movePlayer", "playerId": 2, "target": { "x": 30.0, "y": 20.0 } }
  ],
  "checkpoints": [
    { "tick": 0, "stateHash": "172f58f16ce4360e", "eventHash": "cbf29ce484222325" },
    { "tick": 30, "stateHash": "...", "eventHash": "..." }
  ]
}
```

The example shortens the player list and the tactic; a real file lists every
player and the whole tactic.

| Field           | JSON type | Meaning                                                       |
|-----------------|-----------|---------------------------------------------------------------|
| `schemaVersion` | number    | Version of this format. Currently `3`.                        |
| `coreVersion`   | string    | The `sim-core` version that recorded the match.               |
| `createdAt`     | string    | Creation time in UTC, `%Y-%m-%dT%H:%M:%SZ`. Metadata only.    |
| `seed`          | string    | Unsigned 64-bit master seed, in decimal.                      |
| `gameTime`      | number    | The tick the match was recorded up to.                        |
| `config`        | object    | Parameters of the standard systems (`MatchConfig`).           |
| `initialState`  | object    | The match state at tick 0: every field of `MatchState` except the perception memories, which are empty in every initial state: the recorder rejects a state copied from a running match that already remembers something. |
| `commands`      | array     | Every applied command, in execution order.                    |
| `checkpoints`   | array     | State and event hashes after the steps that reached these ticks. |

Positions are meters and velocities meters per second, as in the
[match state](match-state.md). A player's `target` is `null` when he has none,
the ball's `owner` is `null` while it is free, and its `lastTouch` is `null` or
`{ "playerId": 7, "tick": 120 }`. The pending pass is not recorded: every
initial state has none. `tactics` holds each side's tactic as a complete
[tactic file](tactic-format.md) document, or `null` for a scripted side; a
replay's tactic can be cut out and loaded as a tactic file. An invalid tactic is
rejected with `kInvalidSetup`, a malformed one with `kMalformed`.

### Commands and their order

Commands are listed in execution order: by `tick`, and within a tick by `order`,
which counts `0, 1, 2, …`. Both are explicit, and a reader rejects a file whose
array order disagrees with them instead of re-sorting it: a silently re-sorted
command log is a different match. Commands of one tick execute in the order they
were scheduled (see [match loop](match-loop.md), section *Commands*).

| `type`       | Fields                 | Command             |
|--------------|------------------------|---------------------|
| `movePlayer` | `playerId`, `target`   | `MovePlayerCommand` |
| `giveBall`   | `playerId`             | `GiveBallCommand`   |
| `pass`       | `playerId`, `target`, `speed`, `receiver` (or `null`) | `PassCommand` |

A recorded replay holds the commands the simulation applied. A command scheduled
during the run is included; one scheduled for a tick the match never reached is
not.

### Checkpoints

`stateHash` is `hashMatchState()` of the state after the step that reached `tick`,
as 16 lowercase hexadecimal digits: a stable FNV-1a hash over every field of the
state (`matchStateHash.hpp`). `eventHash`, in the same notation, covers the
[events](match-events.md) of the match so far: every event published by the
steps up to and including that one, in order, fed with `addEvent()` to one
`StableHasher`. At tick 0 no event has happened and it is the hasher's initial
value, `cbf29ce484222325`. The state hash shows that playback ends up in the same
state, the event hash that it got there through the same passes, receptions and
possession changes. The recorder takes one of the initial state (tick
0), one every 30 ticks by default, and one of the final state. Checkpoints are
ascending, unique, and never past `gameTime`; the first is at tick 0 and the last
at `gameTime`, so playback always compares the initial and the final state. Every
command runs before `gameTime`: a command at or after it would never be applied
and is not part of the recorded match.

## Playing a replay back

`playReplay()` rebuilds the match from `initialState`, `config`, `seed` and
`commands` with the standard systems, runs it to `gameTime`, and compares the
state and event hashes at every checkpoint. It reports the first mismatch with the
tick and both hashes. Recording, saving, loading and playing back reproduce every
checkpoint exactly; the unit tests in `tests/unit/sim-replay` hold that.

## Rejected files

Every rejection carries a `ReplayErrorCode` and a message naming the offending
field by its path, for example `initialState.players[3].position.x: expected a
number`.

| Code                        | Cause                                                                  |
|-----------------------------|------------------------------------------------------------------------|
| `kIoError`                  | the file cannot be read or written                                     |
| `kMalformed`                | not JSON, a missing or mistyped field, commands out of order           |
| `kUnsupportedSchemaVersion` | a `schemaVersion` other than 3                                         |
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
| 3       | Checkpoints add `eventHash`. Version 2 files are rejected; record the scenario again with the same seed to get a version 3 file of the same match. |
