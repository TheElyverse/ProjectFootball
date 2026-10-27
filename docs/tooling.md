# Tooling

Everything that records, replays, inspects and measures matches: the replay
format, decision traces, the web debug viewer, match analytics and
`sim-benchmark`.

- [Replay format](#replay-format)
- [Decision trace](#decision-trace)
- [Debug viewer](#debug-viewer)
- [Match analytics](#match-analytics)
- [sim-benchmark](#sim-benchmark)

## Replay format

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

### Schema version 13

```json
{
    "schemaVersion": 13,
  "coreVersion": "0.29.0",
  "createdAt": "2026-09-24T10:00:00Z",
  "seed": "18446744073709551615",
  "gameTime": 300,
  "config": {
    "ticksPerSecond": 30,
    "ball": {
      "rollingDeceleration": 1.5,
      "carryDistance": 0.5,
      "gravity": 9.81,
      "airDrag": 0.33,
      "bounceRestitution": 0.6,
      "bounceGrip": 0.8,
      "spinTransfer": 0.2,
      "spinDecay": 0.4,
      "restingVerticalSpeed": 0.25,
      "minCarrySpeedFactor": 0.8,
      "maxCarrySpeedFactor": 0.95
    },
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
      "pressureErrorFactor": 1.0,
      "maxLoftedSpeed": 28.0,
      "loftedErrorFactor": 2.0
    },
    "reception": {
      "controlRadius": 1.0,
      "reclaimDelaySeconds": 0.3,
      "controlHeight": 1.0,
      "handsRadius": 1.2,
      "handsHeight": 2.2
    },
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
                "riskWeight": 0.3,
        "loftedBias": 0.0
      },
      "shooting": {
        "minConfidence": 0.3,
        "maxShotDistance": 25.0,
        "minOpening": 0.1,
        "zoneColumns": 7,
        "zoneRows": 5,
        "shotSpeed": 25.0,
        "keeperDiveReach": 2.0,
        "keeperJumpReach": 2.6,
        "keeperDiveSeconds": 0.5,
        "blockReach": 1.8,
        "blockMarginSeconds": 0.15,
        "saveMarginSeconds": 0.4,
        "spreadAtZero": 0.2,
        "spreadPerMeter": 0.04,
        "pressureRadius": 3.0,
        "pressureSpread": 1.0,
        "frameMargin": 0.1,
        "reboundShare": 0.3,
        "maxBlockRisk": 0.7,
        "minGoalChance": 0.05,
        "goalWeight": 2.5,
        "secondBallWeight": 0.5,
        "lossWeight": 1.0
      },
      "distribution": {
        "holdSeconds": 2.0,
        "clearSeconds": 6.0,
        "throwRange": 25.0,
        "minLongDistance": 25.0,
        "flightSeconds": 3.0,
        "contestMarginSeconds": 1.0,
        "directnessWeight": 0.5
      },
      "stuckSeconds": 3.0,
      "clearTouchlineMargin": 5.0,
      "clearGoalLineMargin": 10.0,
      "carrying": {
        "enabled": true,
        "minConfidence": 0.3,
        "horizonSeconds": 2.0,
        "minCarryDistance": 2.0,
        "gapRadius": 20.0,
        "sameDirection": 0.98,
        "pitchMargin": 1.0,
        "safetySeconds": 0.3,
        "lossMarginSeconds": 0.4,
        "touchSeconds": 0.5,
        "minTouchDistance": 1.0,
        "maxTouchDistance": 5.0,
        "touchShare": 0.5,
        "lookBackSeconds": 0.3,
        "turnDistance": 3.0,
        "dribbleRadius": 6.0,
        "dribbleCone": 0.5,
        "dribbleClearance": 2.5,
        "dribbleBeyond": 1.5,
        "maxTouchError": 3.0,
        "minTouchError": 0.5,
        "maxDribbleError": 1.5,
        "minDribbleError": 0.5,
        "slowDribbleShare": 1.2,
        "quickDribbleShare": 0.8,
        "shotDistance": 25.0,
        "fullOpening": 0.6,
        "minKeepChance": 0.35,
        "pressureRadius": 6.0,
        "keepWeight": 0.8,
        "progressionWeight": 0.8,
        "spaceWeight": 0.2,
        "shotWeight": 0.6,
        "bypassWeight": 0.15,
        "riskWeight": 0.3,
        "holdWeight": 0.5,
        "turnCost": 0.6,
        "turnSeconds": 0.5,
        "buildUpFactors": { "progression": 1.0, "space": 1.0, "shot": 1.0, "bypassed": 1.0, "risk": 1.5 },
        "progressionFactors": { "progression": 1.0, "space": 1.0, "shot": 1.0, "bypassed": 1.0, "risk": 1.0 },
        "finalThirdFactors": { "progression": 1.0, "space": 1.0, "shot": 2.0, "bypassed": 1.0, "risk": 1.0 },
        "attackingTransitionFactors": { "progression": 2.0, "space": 1.0, "shot": 1.0, "bypassed": 2.0, "risk": 0.8 }
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
      "idleSeconds": 2.0,
      "idlePressRadius": 3.0,
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
    "duels": {
      "intervalTicks": 3,
      "protectSeconds": 0.5,
      "engageRadius": 1.5,
      "breakRadius": 3.0,
      "jockeyDistance": 1.0,
      "turnStep": 2.0,
      "accelerateStep": 4.0,
      "tackleReach": 1.8,
      "footRadius": 0.3,
      "bodyRadius": 0.35,
      "footError": 0.3,
      "minHold": 0.3,
      "maxHold": 0.9,
      "looseSpeed": 5.0,
      "contactDistance": 1.2,
      "shovePerSecond": 1.0,
      "commitCost": 0.3,
      "turnCost": 0.2,
      "recoveryPerSecond": 0.5,
      "stumbleSeconds": 0.5,
      "shieldWeight": 0.8,
      "turnWeight": 0.4,
      "accelerateWeight": 0.6,
      "jockeyWeight": 0.3,
      "commitWeight": 1.0,
      "decisionTemperature": 0.2
    },
    "pressing": {
      "intervalTicks": 6,
      "triggerRadius": 20.0,
      "recentSeconds": 1.5,
      "heavyTouchSpeed": 11.0,
      "slowPassSpeed": 6.0,
      "isolationRadius": 10.0,
      "facingOwnGoal": -0.3,
      "backPassMeters": 2.0,
      "maxJoiners": 4,
            "phaseIntensity": 0.5,
      "maxPressSeconds": 4.0
    },
    "restarts": { "enabled": false, "maxHoldSeconds": 8.0 },
    "shooting": {
      "spreadAtZero": 0.2,
      "spreadPerMeter": 0.04,
      "speedError": 0.05,
      "maxLaunchSlope": 1.0,
      "topspin": 30.0,
      "spinError": 15.0,
      "pressureErrorFactor": 1.0,
      "balanceErrorFactor": 1.0,
      "weakFootErrorFactor": 1.0,
      "unsettledErrorFactor": 1.0,
      "weakFootSide": 0.3,
      "unsettledSeconds": 1.0,
      "unsettledBallSpeed": 20.0,
      "deflectionSpeed": 10.0,
      "blockRadius": 0.5,
      "blockReach": 1.8,
      "minDeflectedSpeed": 0.1,
      "maxDeflectedSpeed": 0.9,
      "blockedBelow": 0.5,
      "deflectionSpread": 1.0,
      "deflectionLift": 3.0
    },
    "woodwork": { "radius": 0.06, "restitution": 0.5 },
    "goalkeeper": {
      "lineDepth": 1.5,
      "highDepthShare": 0.5,
      "possessionDepthShare": 0.5,
      "positionErrorAlong": 1.5,
      "positionErrorAcross": 0.6,
      "cautiousMargin": 0.5,
      "boldMargin": -0.2,
      "sweepHysteresis": 0.1,
      "misjudgement": 0.8
    },
    "shotStopping": {
      "slowestReaction": 0.3,
      "quickestReaction": 0.1,
      "readError": 0.8,
      "readyHeight": 1.4,
      "diveReach": 2.8,
      "jumpReach": 2.5,
      "diveSpeed": 6.0,
      "bodyReach": 0.5,
      "catchableSpeed": 40.0,
      "minParrySpeed": 0.2,
      "maxParrySpeed": 0.5,
      "parryTilt": 1.5,
      "parrySpread": 0.5,
      "carelessTryMargin": 1.5,
      "carefulTryMargin": 0.5,
      "wideMargin": 0.5,
      "diveBelowSpeed": 8.0,
      "standingRecovery": 0.3,
      "stretchRecovery": 1.0,
      "reflexRecoveryShare": 0.3333333333333333
    },
    "aerial": {
      "headHeight": 1.9,
      "headRadius": 0.7,
      "lowestJump": 0.3,
      "highestJump": 0.7,
      "timingError": 0.25,
      "attemptMargin": 0.3,
      "contestRadius": 1.5,
      "landingSeconds": 0.6,
      "reachWeight": 4.0,
      "reachCap": 0.3,
      "arrivalWeight": 5.0,
      "strengthWeight": 1.0,
      "bodyWeight": 1.0,
      "keeperAdvantage": 1.0,
      "duelTemperature": 0.3,
      "contestedDrop": 0.3,
      "shotRange": 14.0,
      "shotSpeed": 14.0,
      "shotHeight": 0.5,
      "shotInside": 0.5,
      "passRange": 20.0,
      "knockDownRange": 6.0,
      "passSeconds": 0.8,
      "openDistance": 5.0,
      "passWeight": 0.6,
      "clearanceZone": 35.0,
      "clearanceDistance": 30.0,
      "maxHeaderSpeed": 18.0,
      "decisionTemperature": 0.15,
      "directionError": 0.06,
      "speedError": 0.1,
      "mistimedErrorFactor": 2.0
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
        "attributes": {
          "maxSpeed": 7.5,
          "acceleration": 4.0,
          "shotAccuracy": 0.5,
          "shotTechnique": 0.5,
          "strongFoot": "right",
          "weakFootAccuracy": 0.5,
          "keeperPositioning": 0.5,
          "keeperAnticipation": 0.5,
          "keeperReflexes": 0.5,
          "keeperHandling": 0.5,
          "jumping": 0.5,
          "heading": 0.5,
          "strength": 0.5,
          "ballControl": 0.5,
          "dribbling": 0.5
        },
        "target": null,
        "facing": { "x": 1.0, "y": 0.0 }
      }
    ],
    "ball": {
      "position": { "x": 30.0, "y": 20.0 },
      "velocity": { "x": 6.5, "y": -1.25 },
      "height": 0.0,
      "verticalVelocity": 0.0,
      "spin": 0.0,
            "owner": null,
      "held": false,
      "carryTouch": null,
      "lastTouch": null
    },
    "score": { "home": 0, "away": 0 },
        "tactics": {
      "home": {
        "contentHash": "4741c1e75f6412df",
        "tactic": { "format": "elyverse-tactic", "version": 3, "name": "reference" }
      },
      "away": null
    }
  },
  "commands": [
    { "tick": 0, "order": 0, "type": "movePlayer", "playerId": 7, "target": { "x": 40.0, "y": 10.0 } },
    { "tick": 45, "order": 0, "type": "movePlayer", "playerId": 7, "target": { "x": 10.0, "y": 35.0 } },
        { "tick": 45, "order": 1, "type": "movePlayer", "playerId": 2, "target": { "x": 30.0, "y": 20.0 } }
  ],
  "checkpointIntervalTicks": 30,
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
| `schemaVersion` | number    | Version of this format. Currently `13`.                       |
| `coreVersion`   | string    | The `sim-core` version that recorded the match.               |
| `createdAt`     | string    | Creation time in UTC, `%Y-%m-%dT%H:%M:%SZ`. Metadata only.    |
| `seed`          | string    | Unsigned 64-bit master seed, in decimal.                      |
| `gameTime`      | number    | The tick the match was recorded up to.                        |
| `config`        | object    | Parameters of the standard systems (`MatchConfig`).           |
| `initialState`  | object    | The match state at tick 0: every field of `MatchState` except the perception memories, which are empty in every initial state: the recorder rejects a state copied from a running match that already remembers something. |
| `commands`      | array     | Every applied command, in execution order.                    |
| `checkpointIntervalTicks` | number | Ticks between checkpoints, at least 1: the resolution at which playback locates a divergence. |
| `checkpoints`   | array     | State and event hashes after the steps that reached these ticks. |

Positions are meters and velocities meters per second, as in the
[match state](match-engine.md#match-state). The ball's `height` and `verticalVelocity` are
its third dimension, in meters and meters per second, and `spin` is top- or
backspin in radians per second ([ball movement](ball.md#ball-movement)). A player's `target` is `null` when he has none,
the ball's `owner` is `null` while it is free, `held` whether its owner holds
it in his hands ([goalkeeper distribution](goalkeeper.md#distribution)),
`carryTouch` `null` or `{ "tick": 118, "seconds": 0.4 }` while it rolls ahead of
its owner after a touch ([ball carrying](on-ball.md#ball-carrying)), and its
`lastTouch` is `null` or `{ "playerId": 7, "tick": 120 }`. A player's `strongFoot` is `"left"` or
`"right"`, and `score` is the goals the match starts at, never negative
([shooting](on-ball.md#shooting)). No pending action is recorded: every
initial state has none. `tactics` holds, for each side, the tactic as a complete
[tactic file](tactics.md#tactic-file-format) document under `tactic` next to its
`contentHash` (`tacticHash.hpp`, 16 hexadecimal digits), or `null` for a
scripted side; a replay's tactic can be cut out and loaded as a tactic file,
and the hash names the exact version played. A reader recomputes the hash and
rejects a tactic whose content does not match it with `kInvalidSetup`, as it
does an invalid tactic; a malformed one is `kMalformed`. Every tactic of a
`changeTactic` command is written the same way.

#### Commands and their order

Commands are listed in execution order: by `tick`, and within a tick by `order`,
which counts `0, 1, 2, …`. Both are explicit, and a reader rejects a file whose
array order disagrees with them instead of re-sorting it: a silently re-sorted
command log is a different match. Commands of one tick execute in the order they
were scheduled (see [match loop](match-engine.md#match-loop), section *Commands*).

| `type`       | Fields                 | Command             |
|--------------|------------------------|---------------------|
| `movePlayer` | `playerId`, `target`   | `MovePlayerCommand` |
| `giveBall`   | `playerId`             | `GiveBallCommand`   |
| `pass`       | `playerId`, `target`, `speed`, `receiver` (or `null`) | `PassCommand` |
| `changeTactic` | `side`, `tactic` (`contentHash` and `tactic` as in `initialState.tactics`) | `ChangeTacticCommand` |

A recorded replay holds the commands the simulation applied. A command scheduled
during the run is included; one scheduled for a tick the match never reached is
not.

#### Checkpoints

`stateHash` is `hashMatchState()` of the state after the step that reached `tick`,
as 16 lowercase hexadecimal digits: a stable FNV-1a hash over every field of the
state (`matchStateHash.hpp`). `eventHash`, in the same notation, covers the
[events](match-engine.md#match-events-and-decision-diagnostics) of the match so far: every event published by the
steps up to and including that one, in order, fed with `addEvent()` to one
`StableHasher`. At tick 0 no event has happened and it is the hasher's initial
value, `cbf29ce484222325`. The state hash shows that playback ends up in the same
state, the event hash that it got there through the same passes, receptions and
possession changes. The recorder takes one of the initial state (tick
0), one every `checkpointIntervalTicks` ticks — 30 by default, `sim-cli
--checkpoint-interval` sets it — and one of the final state. Checkpoints are
ascending, unique, and never past `gameTime`; the first is at tick 0 and the last
at `gameTime`, so playback always compares the initial and the final state. Every
command runs before `gameTime`: a command at or after it would never be applied
and is not part of the recorded match.

### Playing a replay back

`playReplay()` rebuilds the match from `initialState`, `config`, `seed` and
`commands` with the standard systems, runs it to `gameTime`, and compares the
state and event hashes at every checkpoint. The first checkpoint that differs
ends the playback with `kCheckpointMismatch` and a `ReplayDivergence`: the last
checkpoint that still matched, the first that did not, and whether the state
hash, the event hash or both differ there:

```text
diverged after tick 90, at or before tick 120: state hash at tick 120 is 5c0e…, the replay recorded 5c0f…
```

The first differing tick lies in between. A replay recorded with a checkpoint
per tick (`--checkpoint-interval 1`) names it exactly, at the cost of a larger
file. Recording, saving, loading and playing back reproduce every checkpoint
exactly; the unit tests in `tests/unit/sim-replay` hold that, and the P2
acceptance tests replay matches of every tactical identity.

### Rejected files

Every rejection carries a `ReplayErrorCode` and a message naming the offending
field by its path, for example `initialState.players[3].position.x: expected a
number`.

| Code                        | Cause                                                                  |
|-----------------------------|------------------------------------------------------------------------|
| `kIoError`                  | the file cannot be read or written                                     |
| `kMalformed`                | not JSON, a missing or mistyped field, commands out of order           |
| `kUnsupportedSchemaVersion` | a `schemaVersion` other than 13                                        |
| `kIncompatibleCoreVersion`  | recorded with another `coreVersion`                                    |
| `kInvalidSetup`             | an invalid initial state, command or checkpoint list                   |
| `kSimulationFailed`         | a step of the playback failed                                          |
| `kCheckpointMismatch`       | playback does not reproduce a recorded hash                            |

A replay is only valid for the core version that recorded it. A change to
simulation behavior bumps `coreVersion` and makes older replays unplayable by
design, instead of letting them play back into a different match.

### The seed is a string on purpose

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

### Versioning

`schemaVersion` describes the format of this file. It is a single integer, not a
semver string, and it counts up by one.

Raise it when a reader written against this chapter would misread a new file:

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
| 4       | Tactics carry their `contentHash` next to the tactic, `changeTactic` commands, and `checkpointIntervalTicks`. Versions 2 and 3 are rejected with a message to record the scenario again with the same seed. |
| 5       | The ball gains `height`, `verticalVelocity` and `spin`, the ball configuration its flight and bounce constants, and reception its `controlHeight`. Versions 2 to 4 are rejected with a message to record the scenario again with the same seed. |
| 6       | Players gain `shotTechnique`, `strongFoot` and `weakFootAccuracy`, the state its score and last goal, and the configuration its `shooting` and `woodwork` blocks ([shooting](on-ball.md#shooting)). Versions 2 to 5 are rejected with a message to record the scenario again with the same seed. |
| 7       | Players gain `keeperPositioning` and `keeperAnticipation`, reception its `handsRadius` and `handsHeight`, and the configuration its `goalkeeper` block ([goalkeeper](goalkeeper.md#positioning-and-sweeping)); tactics are files of tactic format version 2. Versions 2 to 6 are rejected with a message to record the scenario again with the same seed. |
| 8       | Players gain `keeperReflexes` and `keeperHandling`, and the configuration its `shotStopping` block ([shot stopping](goalkeeper.md#shot-stopping)). Versions 2 to 7 are rejected with a message to record the scenario again with the same seed. |
| 9       | Players gain `jumping`, `heading` and `strength`, and the configuration its `aerial` block ([aerial duels](duels.md#aerial-duels-and-headers)). Versions 2 to 8 are rejected with a message to record the scenario again with the same seed. |
| 10      | The ball gains `held`, passing its `maxLoftedSpeed` and `loftedErrorFactor`, the pass scoring its `loftedBias`, and the decisions their `distribution` block ([goalkeeper distribution](goalkeeper.md#distribution)); tactics are files of tactic format version 3. Versions 2 to 9 are rejected with a message to record the scenario again with the same seed. |
| 11      | The decisions gain `stuckSeconds`, `clearTouchlineMargin` and `clearGoalLineMargin` ([pass decisions](on-ball.md#without-a-valid-option)), the defensive configuration `idleSeconds` and `idlePressRadius` ([pressing](team-tactics.md#a-carrier-who-stands-on-the-ball)). Versions 2 to 10 are rejected with a message to record the scenario again with the same seed. |
| 12      | Players gain `ballControl` and `dribbling`, the ball `carryTouch`, the ball physics `minCarrySpeedFactor` and `maxCarrySpeedFactor`, and the decisions their `carrying` block ([ball carrying](on-ball.md#ball-carrying)). Versions 2 to 11 are rejected with a message to record the scenario again with the same seed. |
| 13      | The configuration's `challenge` block becomes its `duels` block ([duels](duels.md#duels-on-the-ball)). Versions 2 to 12 are rejected with a message to record the scenario again with the same seed. |

## Decision trace

The decision trace answers "why did he do that, and why did it go wrong?" for a
recorded match. It replays the match with [diagnostics](match-engine.md#decision-diagnostics)
on, lists every decision of the chosen players in the chosen ticks with the
reasons behind it, and follows every pass to its outcome. Tracing changes
nothing: the playback verifies every checkpoint as usual.

### From the command line

```sh
./build/debug/apps/sim-cli/sim-cli --scenario intercepted-pass --seed 7 --ticks 150 \
  --replay-out replay.json
./build/debug/apps/sim-cli/sim-cli --play replay.json --trace trace.txt \
  --trace-players 1,8 --trace-from 0 --trace-to 120
```

`--trace` writes the trace; `--trace-players` (comma-separated ids),
`--trace-from` and `--trace-to` (inclusive ticks) narrow it. Without them every
decision of the match is traced. A trace line reads

```text
t=18 #1 passes to #2 (utility 0.42: completion +0.36 progression +0.26 pressure -0.00 risk -0.19; estimated risk 0.64; 1 options, 9 observed; no shot: goal unseen) because completion -> intercepted by #8 at t=70: decision
t=40 #7 shoots at (60.0, 21.2) 0.43 m high (utility 1.62: goal +1.60 secondBall +0.04 possession -0.02; goal chance 0.64, save 0.21, block 0.03; 3 options, 11 observed) because goal
t=66 #4 pressCarrier #14 (utility 0.97: responsibility +0.60 region -0.47 space +0.19 lane +0.00 urgency +0.68 effort -0.03; 5 options) because urgency
t=81 #5 blockLane #12 (assigned by the team press)
t=66 #9 pressCarrier #1 (idlePress: the carrier stands on the ball)
t=18 #1 keeps the ball: no valid option (0 options, 8 observed; no shot: too far; scanFirstSide toward (12.0, 26.2))
t=18 #1 carries to (33.0, 20.0) (utility 0.98: keep +0.80 progression +0.17 space +0.01 shot +0.00 bypassed +0.00 risk -0.00; estimated risk 0.00; 0 options, 8 observed; no shot: too far) because keep
t=120 #1 dribbles past #8 to (40.4, 14.6) (utility 1.10: keep +0.74 progression +0.06 space -0.04 shot +0.00 bypassed +0.15 risk -0.03; estimated risk 0.08; 0 options, 9 observed; no shot: too far) because keep
t=3 #8 sweeps (reaches the ball in 2.20 s, the first attacker in 4.20 s; misjudged by +0.00 s, needs +0.15 s)
t=0 #1 kicks long to #5 (utility 1.24: completion +0.86 progression +0.42 pressure -0.00 risk -0.04 directness +0.00; estimated risk 0.14; 12 options, 13 observed) because completion -> pending
```

- **Observations**: how many entities the player remembered when he decided.
- **Carries**: holding, turning, carrying and dribbling read `holds the ball`,
  `turns toward`, `carries to` and `dribbles past #n to`, with the parts of
  `carryContributions()` -- a turn's time as `turn` -- and the estimated risk of
  losing the touched ball ([ball carrying](on-ball.md#ball-carrying)). Holding the ball
  while he looks for an option reads as keeping it: `looks for a better option`
  with a valid pass or shot, `no valid option` without.
- **Candidates**: how many passes he had, and the chosen option's utility split
  into its weighted parts — `passContributions()` for a pass
  ([pass candidates](on-ball.md#pass-candidates)), `shotContributions()` for a shot
  with its aim and its goal, save and block chances
  ([shot decisions](on-ball.md#shot-decisions)), `ActionScores` for an action without
  the ball ([off-ball movement](team-tactics.md#off-ball-movement)). A goalkeeper
  `throws to` or `punts to` a teammate from his hands and `kicks long to` one
  from his feet, a long ball's utility with its `directness`
  ([goalkeeper distribution](goalkeeper.md#distribution)). A goalkeeper's call to
  come for a ball (`sweeps`) or not (`stays home`) gives both arrivals, his
  misjudgement and the head start he needed instead
  ([goalkeeper](goalkeeper.md#positioning-and-sweeping)).
- **No shot**: when he did not shoot, why: the best shot lost the choice, or
  even the best zone of the goal was not on — `too far`, `goal unseen`,
  `too narrow`, `blocked`, `unlikely` or `not worth it`, with its goal chance
  where it was scored.
- **Reason**: the part with the largest absolute weight (`dominantContribution()`,
  `dominantShotContribution()`, `dominantScore()`). A carrier without a valid
  option ([pass decisions](on-ball.md#without-a-valid-option)) names
  how he dealt with it instead: `scanFirstSide` or `scanOtherSide` toward the
  point he looks at, `forcedRejected` for the best rejected pass he plays once
  stuck, `forcedClear` for his long kick when he has no pass at all. An action
  marked `idlePress` sent a player at a carrier who stood on the ball
  ([pressing](team-tactics.md#a-carrier-who-stands-on-the-ball)).
- **Outcome** of a pass (a shot's outcome comes with shot execution): `received`, `intercepted`, `recovered by the passer`,
  `not played` (he lost the ball first) or `pending` at the end of the trace.

### Why a pass failed

An intercepted pass is attributed to one stage of the decision pipeline
(`attributeInterception()`), checked in this order:

| Cause | When |
|---|---|
| `perception` | the passer had not seen the interceptor — no observation of at least the scoring's `minConfidence` — or believed him more than `perceptionErrorMeters` (3 m) from where he really was when he decided |
| `decision` | he saw him and still chose a pass with an estimated interception risk of at least `riskyDecision` (0.5) |
| `execution` | he saw him and judged the pass safe: the kick itself — direction and speed error, pressure on the passer — let it fail |

The limits are `TraceConfig`; they classify, they do not change the match.

### In code

```cpp
DecisionTracer tracer(replay.setup.config);
const auto playback = playReplay(
    replay, {.diagnostics = DiagnosticsFilter{.players = {PlayerId(4)}, .from = {}, .to = {}},
             .afterStep = [&tracer](const MatchSimulation& simulation) {
               tracer.recordStep(simulation);
             }});
const std::string text = formatTrace(tracer.entries());
```

`DecisionTracer` works on any simulation with diagnostics on, not only on a
playback. Its entries hold the full diagnostics, for tools that want more
than the text.

### Cost

Diagnostics are built only for the filtered players in the filtered ticks; the
rest of the match runs at full speed. With diagnostics off — every normal run
— a decision pays one pointer check.

## Debug viewer

The debug viewer shows a match in 2D: the pitch, both teams, the ball and who
has it, and for a selected player what he is running to, what he sees, what he
remembers and how he decided his last pass. It is a static web page
(`apps/sim-viewer`, TypeScript and Canvas) that plays back **debug frames**
written by `sim-cli`.

The viewer never simulates. `sim-cli` runs the match headlessly, exactly as it
does without the viewer, and writes one frame per tick; the page only reads that
file. Pausing, stepping, scrubbing, changing the speed or selecting a player
changes what is on screen, never what happens in the match.

### Watch a match

Record a scenario with frames next to its replay:

```sh
./build/debug/apps/sim-cli/sim-cli --scenario m0-acceptance --seed 42 --ticks 900 \
  --replay-out replay.json --frames-out frames.json
```

Then build and serve the viewer (Node.js 22 or newer, and pnpm):

```sh
cd apps/sim-viewer
pnpm install
pnpm run build
pnpm run serve ../../frames.json
```

and open the printed URL, `http://localhost:8080/?frames=frames.json`. Without a
file argument, `pnpm run serve` serves the page alone and **Frames** loads a file
from disk. The page needs a server rather than `file://` because browsers do not
load JavaScript modules from files; `scripts/serve.mjs` is a few lines of
`node:http` bound to `127.0.0.1`, and `--port <n>` changes the port.

Collecting frames does not change the match: the replay and the final state
hash are the same with or without `--frames-out`, and every frame carries the
state hash of its tick, so a frame can be matched against a replay checkpoint.

### What it shows

| On the pitch                   | Meaning                                                      |
|--------------------------------|--------------------------------------------------------------|
| blue / red disc with a number  | a home / away player and his id; the white stroke is his facing |
| yellow ring                    | the ball's owner                                             |
| white disc                     | the ball; a flying ball grows with its height and casts a shadow on the grass |
| dashed yellow line             | a pending pass, from the passer to its target               |
| dashed white line and cross    | a movement target (selected player, or all with **all targets**) |

Clicking a player selects him; Escape or a click on empty grass clears it. For
the selected player the pitch adds:

- his **vision cone**, `viewDistance` deep and `fieldOfViewDegrees` wide around
  where he looks -- his facing, or a carrier's look over his shoulder for an
  option ([pass decisions](on-ball.md#without-a-valid-option)) -- and
  his **awareness radius**, the circle in which he notices
  players behind him ([perception](players.md#perception));
- his **observations**: a dotted circle where he remembers each player and the
  ball, fainter as its confidence falls;
- his **latest pass decision**: a line from where he decided to every candidate
  target, green if valid, grey and dashed if rejected, the chosen pass yellow
  and thick ([pass candidates](on-ball.md#pass-candidates), [pass decisions](on-ball.md#pass-decisions));
- his **latest action decision** without the ball: a dashed white line and ring
  at every candidate target, the chosen one yellow
  ([off-ball movement](team-tactics.md#off-ball-movement), [defensive shape](team-tactics.md#defensive-shape)).

In a match with tactics, the checkboxes below the controls switch the tactical
overlays, drawn under the players:

| Overlay           | On the pitch                                                  |
|-------------------|---------------------------------------------------------------|
| **pitch control** | each cell of the latest [pitch control](team-tactics.md#pitch-control) grid in the colour of the side that controls it, the stronger the clearer its control |
| **zones**         | dotted lines between the five lanes, dashed lines between the thirds ([zones](team-tactics.md#zones-and-team-shape)) |
| **regions**       | each player's desired region: a ring at its centre joined to him, a dot at its tactical target ([desired region](team-tactics.md#desired-region)) |
| **lines**         | each side's defensive line (solid) and front line (faint), and the defensive line its phase's instruction asks for (dashed) |
| **press**         | a running press: a ring around the pressed carrier, pressers joined to him, each blocked lane dashed from the carrier to the receiver it cuts off with its blocker joined to it, covers dotted to whom they cover ([pressing](team-tactics.md#pressing)) |

Each side's tactic and phase stand above its half of the pitch.

The side panel lists the match (tick, time, state hash, ball owner, last touch,
and how fast and how high the ball is); each side's tactic, phase, defensive line against the one asked for,
length and width, and running press with its trigger and roles; the selected
player's position, speed, target, observations, his latest action decision
(action, subject, utility and the dominant reason) and the candidate table of
his latest pass decision (`*` marks the choice); and the latest
[events](match-engine.md#match-events-and-decision-diagnostics), newest first.

| Control            | Keys                         | Effect                                 |
|--------------------|------------------------------|----------------------------------------|
| Play / Pause       | Space                        | play at the chosen speed, or pause    |
| ◀ / ▶              | Left / Right (Shift: ten)    | step one tick back or forward, paused |
| speed              |                              | 0.25x to 8x match time                 |
| slider             |                              | jump to any tick                       |

### Frame format

`sim-cli --frames-out` writes one JSON document. It is read-only output for the
viewer, versioned separately from the [replay format](#replay-format): nothing
reads it back into a simulation, and a replay, not a frame file, is what
reproduces a match. Positions, velocities and scores are rounded to three
decimals, which keeps a 30-second match of fourteen players around 17 MB.
`--frames-out` records at most 3600 ticks (two minutes at 30 Hz,
`kMaxFrameTicks`): the recording is held in memory until it is written, and a
whole match would run to gigabytes that neither the CLI nor a browser handles
well. The viewer is for looking at situations; a replay records any length. An
abridged example with illustrative values:

```json
{
    "format": "elyverse-debug-frames",
  "version": 4,
  "coreVersion": "0.29.0",
  "scenario": "m0-acceptance",
  "seed": "42",
  "ticksPerSecond": 30,
  "perception": { "viewDistance": 60, "fieldOfViewDegrees": 180, "awarenessRadius": 3 },
  "reception": { "controlRadius": 1 },
  "pitch": { "length": 60, "width": 40 },
  "players": [{ "id": 1, "side": "home" }],
  "frames": [
    {
      "tick": 1,
      "stateHash": "d96934b44b13ded7",
      "ball": {
        "position": [30, 20], "velocity": [0, 0],
        "height": 0, "verticalVelocity": 0, "spin": 0,
        "owner": null, "lastTouch": null
      },
      "score": { "home": 0, "away": 0 },
      "pendingPass": null,
      "players": [
        {
          "id": 1,
          "position": [3, 20],
          "velocity": [0, 0],
          "facing": [1, 0],
          "look": [1, 0],
          "target": null,
          "observations": [
            { "entity": "ball", "position": [30, 20], "velocity": [0, 0], "confidence": 1, "lastSeen": 0 }
          ]
        }
      ],
      "events": [{ "tick": 0, "type": "possessionChanged", "previousOwner": null, "newOwner": 4 }],
      "decisions": [
        {
          "tick": 0,
          "player": 4,
          "outcome": "passed",
          "chosen": 0,
          "reason": "choice",
          "target": null,
          "candidates": [
            {
              "receiver": 7, "target": [40, 15], "distance": 12.1, "speed": 9.6,
              "receiverConfidence": 1, "interceptionRisk": 0.02, "completion": 0.98,
              "progression": 0.12, "receiverPressure": 0.1, "utility": 1.02, "rejection": "valid"
            }
          ],
          "observations": []
        }
      ]
    }
  ]
}
```

- The header holds what never changes during a match: the run's identity (core
  version, scenario, seed as a decimal string like in a replay), the tick rate,
  the perception and reception parameters the viewer draws, the pitch, and the
  squad with each player's side.
- `frames` has one entry per tick from 0, the initial state, to the last
  simulated tick. A frame is the state after the step that reached its tick,
  plus the events and decision diagnostics that step recorded; frame 0 has
  none. Events and decisions carry their own `tick`, the step's, which is one
  less than the frame's: the step from tick 18 reaches frame 19. The viewer
  shows that tick in the event log and the decision panel.
- Vectors are `[x, y]` arrays in meters, `x` along the pitch length and `y`
  across it. Ids are player ids; `null` means none.
- `observations` of a player are his memory after the step. The observations
  of a decision are the memory the decision was made from, which is the
  previous frame's.
- The header's `zones` hold the pitch `y` of every boundary between lanes and
  the pitch `x` of the boundaries between thirds ([zones](team-tactics.md#zones-and-team-shape)).
- Frames carry `teams`: each side's `tactic` name, `phase`, the phase's
  `instruction` (`lineHeight`, `blockLength`, `blockWidth` as fractions of the
  pitch, `pressingIntensity`), its running `press` (`carrier`, `since`,
  `trigger`, `assignments` of `player`, `role` and `subject`) and its `shape`
  (lines as depths from its own goal line, length, width, centroid). Each
  player has his `region` (tactical target, centre, cost) and `action` (type,
  target, subject), and `actions` lists the decisions of players without the
  ball, each candidate with its weighted `scores`, `utility` and `dominant`
  part; see [off-ball movement](team-tactics.md#off-ball-movement),
  [defensive shape](team-tactics.md#defensive-shape) and [pressing](team-tactics.md#pressing). They are
  `null` or empty for a scripted side.
- `pitchControl` is home's control of every cell of the
  [pitch control](team-tactics.md#pitch-control) grid, column by column, rounded to
  hundredths -- away's is the rest -- in the frames whose step refreshed the
  grid, `null` in all others: the viewer shows the latest.
- Event `type`s are the [match events](match-engine.md#match-events-and-decision-diagnostics) in lower camel case --
  `passAttempted`, `pressingStarted`, `pitchControlSampled`, ... -- with their
  fields; sides and phases are spelled `home`,
  `buildUp`, ... as in [tactics](tactics.md#tactic-model). The viewer shows an event type it
  does not know by its name. A decision's `outcome` is `passed`, `shot` or
  `noValidOption`, `chosen` indexes `candidates` (null for a shot), and `rejection` is `valid` or
  the reason a candidate cannot be played.

A change that breaks existing readers raises `version`; the viewer rejects
versions it does not know. Version 2 added `zones`, `pitchControl` and the
teams' `tactic`, `instruction` and `press`. Version 3 added the ball's `height`,
`verticalVelocity` and `spin` ([ball movement](ball.md#ball-movement)). Version 4 added
the decision outcome `shot` and the `shotAttempted` event
([shot decisions](on-ball.md#shot-decisions)). Version 5 added every frame's `score`,
`struckAt` and `struckHeight` to `shotAttempted`, and the events `shotDeflected`,
`shotHitWoodwork`, `shotResolved` and `goalScored` ([shooting](on-ball.md#shooting)).
The events `saveAttempted` ([shot stopping](goalkeeper.md#shot-stopping)) and
`aerialContest` ([aerial duels](duels.md#aerial-duels-and-headers)) came without a new version: a
viewer that does not know them shows them by their name. So did the ball's
`held`, the `lofted` of the pending pass and of a pass candidate, and a
decision's `held` ([goalkeeper distribution](goalkeeper.md#distribution)): a
viewer that does not know a field ignores it. So did a player's `look`, a
decision's `reason` and `target` and an action's `idlePress`
([pass decisions](on-ball.md#without-a-valid-option)): the viewer draws
the vision cone of a player without a `look` along his facing.

### Development

`apps/sim-viewer` depends on TypeScript only, pinned to an exact version;
`pnpm-lock.yaml` is committed, so an install with `--frozen-lockfile` — as CI and
`make viewer-test` run it — resolves to exactly that version. pnpm comes from the
`packageManager` field via Corepack. Behind a proxy that blocks
`registry.npmjs.org`, point pnpm at a mirror with `registry=<url>` in an
`.npmrc`; pnpm honours that setting like any other client.

| Path              | Contents                                                        |
|-------------------|-----------------------------------------------------------------|
| `src/frames.ts`   | the frame format's types, parsing and lookups                   |
| `src/geometry.ts` | pitch-to-canvas mapping, vision cones, picking a player         |
| `src/playback.ts` | which frame is shown: play, pause, step, seek, speed            |
| `src/render.ts`   | drawing a frame on the canvas                                   |
| `src/overlays.ts` | the tactical overlays                                           |
| `src/panel.ts`    | the side panel                                                  |
| `src/main.ts`     | wiring the page together                                        |
| `test/`           | `node:test` tests of the pure modules, run on the compiled output |

`pnpm test` type-checks, compiles to `dist/` and runs the tests; CI runs it in
the `viewer` job, and `make viewer-test` runs install and tests from the
repository root.

## Match analytics

`libs/sim-analytics` turns a match's [domain events](match-engine.md#match-events-and-decision-diagnostics) into
statistics. It is read-only: it never runs a system, never writes a state and
reads no state beyond the match context. Everything it measures is in the
event stream, so a match can be analysed while it runs, from a recording of its
events, or by replaying it.

### Use

```cpp
MatchAnalyzer analyzer(contextOf(setup.initialState, setup.config.ticksPerSecond));
while (simulation.tick() < end) {
  simulation.step();
  analyzer.observeStep(simulation.events());
}
const MatchStats stats = analyzer.finish(simulation.tick());
const std::string json = toStatsJson(stats);
```

The `MatchContext` holds what is fixed for a match: the pitch length, the tick
rate and which side each player plays for. `observeStep()` takes the events of
one step in order; events of players the context does not know are ignored.
`finish(tick)` closes the last possession at `tick`, the tick after the last
step.

`sim-cli` writes the statistics of a run with `--stats-out`:

```sh
./build/debug/apps/sim-cli/sim-cli --home-tactic data/tactics/pressing.json --seed 2 \
  --ticks 1800 --stats-out stats.json
```

### Metrics

Every metric is per side. Depths and thirds are measured in the side's own
frame: from its own goal line toward the one it attacks.

| Metric | From | Definition |
|---|---|---|
| `possessionShare` | `PossessionChanged` | ticks the side had the ball over ticks either side had it. A free ball — a pass in flight — belongs to the side that last owned it; the time before the first owner belongs to nobody |
| `longestOwnershipSeconds` | `PossessionChanged` | the longest time one player of the side owned the ball, from the change that gave it to him to the next change or the end; an ownership the match starts with is not counted. A player who never plays the ball shows here |
| `passes`, `completedPasses`, `passCompletion` | `PassAttempted`, `PassReceived` | attempts, receptions by a teammate, and their ratio |
| `meanPassMeters` | `PassAttempted` | mean distance from `from` to `target` |
| `progressivePasses`, `completedProgressivePasses` | `PassAttempted`, `PassReceived` | passes whose target is at least `progressiveMeters` (10 m) deeper than their origin, and those received |
| `shots` | `ShotAttempted` | shots taken ([shot decisions](on-ball.md#shot-decisions)); what became of them is in the [shooting](on-ball.md#shooting) events, which the statistics do not read yet |
| `turnovers`, `regains` | `PossessionChanged` | the ball's owner changes from one side to the other; the first owner of the match is neither |
| `regainsByThird` | `PassIntercepted`, `BallWon`, `LooseBallRecovered` | regains by the third of their position; a regain without such an event in its step, a command giving the ball away for instance, is counted in `regains` only |
| `pressures`, `pressuresRegained` | `PressingStarted`, `PressingEnded` | coordinated presses and those that ended with `ballRegained` |
| `ppda` | `PassAttempted`, `BallWon`, `PassIntercepted` | passes per defensive action: opponent passes from the opponent's build-up zone — the `ppdaZone` (60 %) of the pitch nearest its own goal — over the side's balls won -- in duels or from a touch -- and interceptions in that zone |
| `pitchControlShare` | `PitchControlSampled` | mean share of the pitch the side controls ([pitch control](team-tactics.md#pitch-control)) |
| `attackingThirdShare` | `PitchControlSampled` | share of samples with the ball in the side's attacking third: territory |

Rates and means are empty (`null` in JSON) where their denominator is zero:
`ppda` without a defensive action, `pitchControlShare` without a sample. The
sums `possessionShare` home + away and `turnovers` of one side = `regains` of
the other hold for every match.

`AnalyticsConfig` holds `progressiveMeters` and `ppdaZone`; the analyzer
rejects a negative or non-finite distance and a zone outside `(0, 1]`.

### JSON

`toStatsJson()` writes format `elyverse-match-stats`, version 1:

```json
{
  "format": "elyverse-match-stats",
  "version": 1,
  "ticks": 1800,
  "seconds": 60.0,
  "home": {
    "possessionShare": 0.55,
    "longestOwnershipSeconds": 4.2,
    "passes": 18,
    "...": "...",
    "regainsByThird": { "defensive": 2, "middle": 3, "attacking": 1 },
    "ppda": 4.5,
    "pitchControlShare": 0.51,
    "attackingThirdShare": 0.2
  },
  "away": { "...": "..." }
}
```

Fields appear in the order of the table above, empty values are `null` and
numbers are written in their shortest exact form. Sums are formed in event
order, so the same events always give the same bytes, on every platform.

### What this is not

No goals, shot outcomes, expected goals or player ratings yet: shots are
counted, but not what became of them. Pressures are team presses, not every individual pressing action.

## sim-benchmark

`apps/sim-benchmark` plays a series of seven-a-side matches between two tactic
files and writes their statistics: how each style fares against another over
many matches, with confidence intervals instead of anecdotes. It is also the
performance check of the match simulation.

```sh
./build/release/apps/sim-benchmark/sim-benchmark \
  --home data/tactics/pressing.json --away data/tactics/counter.json \
  --matches 20 --minutes 90 --seed 1 --jobs 4 --out pressing-counter.json
```

| Option                        | Default | Meaning                                              |
|-------------------------------|---------|------------------------------------------------------|
| `--home`, `--away`            |         | the two tactic files, required                       |
| `--matches`                   | 10      | how many matches                                     |
| `--seed`                      | 1       | the base seed of the series                          |
| `--minutes`                   | 10      | match length in simulated minutes                    |
| `--jobs`                      | 1       | matches played at once                               |
| `--out`                       |         | where to write the results file                      |
| `--no-restarts`               |         | leave the ball on the line when it goes out          |
| `--max-seconds-per-match`     |         | fail if a match takes longer, in wall-clock seconds  |

### A round robin of styles

With `--style` instead of `--home` and `--away`, repeated once per tactic file,
the benchmark plays every ordered pairing of the styles — each against itself
too — as a series of `--matches` matches. Pairing `k` (home style by home
style: (0, 0), (0, 1), …) plays with base seed `matchSeed(seed, k)`, so pairings
play unrelated matches.

```sh
./build/release/apps/sim-benchmark/sim-benchmark \
  --style data/tactics/possession.json --style data/tactics/counter.json \
  --style data/tactics/pressing.json \
  --matches 20 --minutes 90 --seed 1 --jobs 2 --verify-replays 1 --out styles.json
```

The results file, format `elyverse-style-benchmark`, version 1, holds the
styles, each style's `styleSummary` over all its matches — home and away —
the `separations`, and every pairing's summaries and matches. A **separation**
is a metric on which one style's 95 % interval lies wholly above another's: a
difference beyond chance. The console lists them.

`--verify-replays n` records the first `n` matches of every series again as a
replay, plays it back and fails the run if a checkpoint differs: the sampled
determinism check.

### The matches

Every match is the [tactic match](scenarios.md#the-tactic-match): the kickoff
fixture with the two tactics, the standard systems and
[restarts](match-engine.md#restarts) on — over whole matches a ball waiting on the line would
blur every statistic. Match `i` plays with seed `matchSeed(base, i)`, SplitMix64
of the base seed advanced by `i + 1` steps: neighbouring matches get unrelated
seeds, and a match's seed depends only on the base seed and its index, never on
how many matches run or in which order. Any match's tactics and seed can be
replayed with `sim-cli --home-tactic ... --away-tactic ... --seed <its seed>`,
but `sim-cli` has no restart option and defaults to `RestartConfig::enabled ==
false`, so a match where the ball goes out of play diverges from `sim-cli`'s
replay of it once it does.

Statistics come from [match analytics](#match-analytics) over each match's
events. A match fails the whole run — naming its index, seed and tick — if a
step fails or produces a non-finite value, or a statistic is not a finite
number.

### Results

The results file is JSON, format `elyverse-benchmark`, version 1: the core
version, both tactics by name and content hash, the series (`matches`,
`baseSeed`, `ticks`, `restarts`), a `summary` per side and metric, and
`results`, every match's index, seed and statistics.

A metric's summary holds its `count` of matches — metrics a match leaves empty,
such as a PPDA without defensive actions, are left out — its `mean`, sample
`variance` and `ci95`, the 95 % confidence interval of the mean,
`mean ± t · s / √n` with Student's t (`seriesSummary.hpp`).

The file holds no timing, and every sum runs in match order. So the same
tactics, series and core version always give **the same bytes**, whatever
`--jobs` is; a test and the benchmark smoke test hold that.

The console shows each match's seed and wall-clock time, the headline metrics
of both sides with their intervals, and the mean and slowest match time.

### P2 acceptance

**In CI**, the `style-benchmark` job builds the `ci-release` preset and runs
`ctest -L benchmark`: `tests/benchmark/p2StyleBenchmark.cmake` plays the round
robin of the three [tactical identities](tactics.md#tactical-identities), eight
six-minute matches per pairing with seed 1 and one sampled replay per pairing.
It must finish without an invalid value within 5 s per match and 120 s for
all 72 (about 30 s at the time of writing), every sampled replay must
reproduce, and the styles must separate where their identities promise:

| Metric                  | Separation                              |
|-------------------------|-----------------------------------------|
| `regains`               | pressing above possession and counter   |
| `regainsAttackingThird` | pressing above counter                  |
| `ppda`                  | counter above pressing — pressing allows fewer passes per defensive action |

These hold for base seeds 1 to 5 of the reduced run. It played six matches
per pairing until the carrier saw the goal along where he
[looks](players.md#perception): six then no longer separated pressing's regains in the
attacking third from counter's with seed 1, eight do with seeds 1 to 5.
Possession's PPDA lies about a tenth above pressing's: thirty matches separate the two, six not with
every seed — not with seeds 3 and 4 at first, nor with seed 1 since the keeper
[distributes](goalkeeper.md#distribution) the ball — and the test does not ask
for it. Pressing also regained
the ball in the attacking third more often than possession while a shot that
went in was a goal kick to win back in front of the goal; since it is a
[goal](on-ball.md#shooting) and a kickoff, six matches no longer separate the two with
every seed — with seeds 2 and 3 but not 1 at first, with seeds 1 and 3 but not 2
since both sides [line up](match-engine.md#the-kickoff) for the kickoff — and the
test does not ask for it. Every side of every
pairing must also shoot between 0.5 and 40 times per six-minute match on
average — a guardrail against a change that stops the shooting or turns the
sandbox into a shooting gallery, set from the first measurement of 2 to 27,
which goals have brought down to 3 to 13, the kickoff line-up to 1 to 9 and
the keeper's [shot stopping](goalkeeper.md#shot-stopping) to 0.67 to 11.67 and
[ball carrying](on-ball.md#ball-carrying) to 1.62 to 11.88, possession against
counter's deep block the fewest ([shot decisions](on-ball.md#shot-decisions)). And in
every match no player of either side may own the ball longer than 15 s at a
time (`longestOwnershipSeconds`, [match analytics](#match-analytics)): a
carrier without an option once kept it for up to 318 s against counter's deep
block; since he [looks round for one](on-ball.md#without-a-valid-option),
a carrier who stands on the ball [is gone at](team-tactics.md#a-carrier-who-stands-on-the-ball)
and one who carries it about gets stuck as one who holds it does
([ball carrying](on-ball.md#holding-on-and-getting-stuck)), the longest
is 10.4 s. The test is registered for Release
builds only; in Debug it would take minutes.

**The full run** is twenty 90-minute matches per pairing, the command above.
Its results at the time of writing (core 0.18.0) are in
[tactical identities](tactics.md#over-whole-matches).

### P2 performance budget

A 90-minute seven-a-side match with the standard systems, restarts on and two
P2 tactics must simulate in **under 10 seconds** of wall-clock time on one core
of a CI-class machine in a Release build. At the time of writing a
pressing-against-counter match takes about 5.8 s (2-core cloud container,
GCC 13, Release): enough headroom for the P3 systems to come without
revisiting it yet. `--max-seconds-per-match 10` checks it.

Debug builds are around ten times slower; the smoke test therefore plays two
one-minute matches only.
