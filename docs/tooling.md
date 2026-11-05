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

A replay file records a match so it can be played back exactly: the replay
contract `InitialSnapshot + OrderedCommands + Seed(s) + CoreVersion` of the
[implementation plan](implementation-plan.md), section 5.3, plus the
configuration of the standard systems, and state and event hashes to check
playback against.

The `sim-replay` library (`replay.hpp`, `replayJson.hpp`) records, writes, reads
and plays back replays; it depends on `sim-match` and on
[nlohmann/json](https://github.com/nlohmann/json). `sim-cli` writes a replay on
every successful run, to `--replay-out` (default `replay.json` in the working
directory); `--play` plays one back. The file is written before the terminal
interface opens, so a run rejected by argument or terminal validation leaves no
file.

### Schema version 23

```json
{
  "schemaVersion": 23,
  "coreVersion": "0.39.0",
  "createdAt": "2026-09-24T10:00:00Z",
  "seed": "18446744073709551615",
  "gameTime": 300,
  "config": {
    "ticksPerSecond": 30,
    "ball": { "rollingDeceleration": 1.5, "carryDistance": 0.5, "...": "..." },
    "perception": { "...": "..." },
    "passing": { "...": "..." },
    "reception": { "...": "..." },
    "pursuit": { "...": "..." },
    "decisions": {
      "intervalTicks": 6,
      "...": "...",
      "scoring": { "...": "..." },
      "shooting": { "...": "..." },
      "distribution": { "...": "..." },
      "carrying": { "...": "...", "buildUpFactors": { "...": "..." } }
    },
    "phases": { "...": "..." },
    "pitchControl": { "...": "..." },
    "positioning": { "...": "..." },
    "offBall": { "...": "..." },
    "defensive": { "...": "..." },
    "duels": { "...": "..." },
    "pressing": { "...": "..." },
    "restarts": { "enabled": true, "maxHoldSeconds": 8.0 },
    "shooting": { "...": "..." },
    "woodwork": { "radius": 0.06, "restitution": 0.5 },
    "goalkeeper": { "...": "..." },
    "shotStopping": { "...": "..." },
    "aerial": { "...": "..." },
    "clock": { "durationMinutes": 90.0, "throwInLostSeconds": 4.0, "...": "..." },
    "fitness": { "sprintSpeedShare": 0.85, "...": "..." },
    "offside": { "enabled": false, "levelMeters": 0.1 },
    "fouls": { "enabled": false, "fromBehindDot": -0.5, "...": "..." },
    "setPieces": { "throwInSeconds": 3.0, "cornerSeconds": 8.0, "...": "..." }
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
        "facing": { "x": 1.0, "y": 0.0 },
        "fitness": { "sprintReserve": 1.0, "playedTicks": 0, "topSpeedShare": 1.0 },
        "yellowCards": 0,
        "sentOff": false
      }
    ],
    "ball": {
      "position": { "x": 30.0, "y": 20.0 },
      "velocity": { "x": 6.5, "y": -1.25 },
      "height": 0.0,
      "verticalVelocity": 0.0,
      "spin": 0.0,
      "sideSpin": 0.0,
      "owner": null,
      "held": false,
      "carryTouch": null,
      "lastTouch": null
    },
    "score": { "home": 0, "away": 0 },
    "tactics": {
      "home": {
        "contentHash": "8ebf9708c91041e5",
        "tactic": { "format": "elyverse-tactic", "version": 4, "name": "reference" }
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

The example shortens the configuration, the player list and the tactic; a real
file holds them all. Each `config` block holds every field of its
configuration, under the names and with the defaults of its topic doc's
configuration table (`BallPhysics` in [ball](ball.md#constants), `DuelConfig` in
[duels](duels.md#configuration), and so on). `sim-cli --replay-out` writes a
complete file.

| Field           | JSON type | Meaning                                                       |
|-----------------|-----------|---------------------------------------------------------------|
| `schemaVersion` | number    | Version of this format. Currently `18`.                       |
| `coreVersion`   | string    | The `sim-core` version that recorded the match.               |
| `createdAt`     | string    | Creation time in UTC, `%Y-%m-%dT%H:%M:%SZ`. Metadata only.    |
| `seed`          | string    | Unsigned 64-bit master seed, in decimal.                      |
| `gameTime`      | number    | The tick the match was recorded up to.                        |
| `config`        | object    | Parameters of the standard systems (`MatchConfig`).           |
| `initialState`  | object    | The match state at tick 0: every field of `MatchState` except the perception memories, which are empty in every initial state: the recorder rejects a state copied from a running match that already remembers something. |
| `commands`      | array     | Every applied command, in execution order.                    |
| `checkpointIntervalTicks` | number | Ticks between checkpoints, at least 1: the resolution at which playback locates a divergence. |
| `checkpoints`   | array     | State and event hashes after the steps that reached these ticks. |

Units are those of the [match state](match-engine.md#match-state): meters and
meters per second; the ball's `height`, `verticalVelocity` and `spin` (rad/s,
top- or backspin) as in [ball movement](ball.md#ball-movement). `null` means
none: a player's `target`, the ball's `owner` while free. `held` says whether
the owner holds the ball in his hands
([goalkeeper distribution](goalkeeper.md#distribution)). `carryTouch` is `null`
or `{ "tick": 118, "seconds": 0.4 }` while the ball rolls ahead of its owner
after a touch ([ball carrying](on-ball.md#ball-carrying)); `lastTouch` is `null`
or `{ "playerId": 7, "tick": 120 }`. `strongFoot` is `"left"` or `"right"`;
`score` is the goals the match starts at, never negative
([shooting](on-ball.md#shooting)). No pending action is recorded: every initial
state has none.

`tactics` holds for each side the tactic as a complete
[tactic file](tactics.md#tactic-file-format) under `tactic`, next to its
`contentHash` (`tacticHash.hpp`, 16 hexadecimal digits), or `null` for a
scripted side. A replay's tactic can be cut out and loaded as a tactic file, and
the hash names the exact version played. A reader recomputes the hash and
rejects a mismatch with `kInvalidSetup`, as it does an invalid tactic; a
malformed one is `kMalformed`. A `changeTactic` command writes its tactic the
same way.

#### Commands and their order

Commands are listed in execution order: by `tick`, and within a tick by `order`
(`0, 1, 2, …`). Both are explicit, and a reader rejects a file whose array order
disagrees, instead of re-sorting it: a re-sorted command log is a different
match. Commands of one tick execute in the order they were scheduled
([match loop](match-engine.md#match-loop), section *Commands*).

| `type`       | Fields                 | Command             |
|--------------|------------------------|---------------------|
| `movePlayer` | `playerId`, `target`   | `MovePlayerCommand` |
| `giveBall`   | `playerId`             | `GiveBallCommand`   |
| `pass`       | `playerId`, `target`, `speed`, `receiver` (or `null`) | `PassCommand` |
| `changeTactic` | `side`, `tactic` (`contentHash` and `tactic` as in `initialState.tactics`) | `ChangeTacticCommand` |

A replay holds the commands the simulation applied: one scheduled during the run
is included, one for a tick the match never reached is not.

#### Checkpoints

`stateHash` is `hashMatchState()` of the state after the step that reached
`tick`, as 16 lowercase hexadecimal digits: a stable FNV-1a hash over every
field of the state (`matchStateHash.hpp`). `eventHash`, in the same notation,
covers the [events](match-engine.md#match-events-and-decision-diagnostics) of
the match so far: every event of the steps up to and including that one, in
order, fed with `addEvent()` to one `StableHasher`. At tick 0 no event has
happened, and it is the hasher's initial value, `cbf29ce484222325`. The state
hash shows that playback reaches the same state, the event hash that it got
there through the same passes, receptions and possession changes.

The recorder takes a checkpoint of the initial state (tick 0), one every
`checkpointIntervalTicks` ticks — 30 by default, set with `sim-cli
--checkpoint-interval` — and one of the final state. Checkpoints are ascending,
unique and never past `gameTime`; the first is at tick 0 and the last at
`gameTime`, so playback always compares the initial and the final state. Every
command runs before `gameTime`; a later one would never apply.

### Playing a replay back

`playReplay()` rebuilds the match from `initialState`, `config`, `seed` and
`commands` with the standard systems, runs it to `gameTime`, and compares the
hashes at every checkpoint. The first checkpoint that differs ends playback with
`kCheckpointMismatch` and a `ReplayDivergence`: the last matching checkpoint,
the first differing one, and whether the state hash, the event hash or both
differ:

```text
diverged after tick 90, at or before tick 120: state hash at tick 120 is 5c0e…, the replay recorded 5c0f…
```

The first differing tick lies in between; a replay with a checkpoint per tick
(`--checkpoint-interval 1`) names it exactly, at the cost of a larger file.
Recording, saving, loading and playing back reproduce every checkpoint exactly;
the unit tests in `tests/unit/sim-replay` hold that, and the P2 acceptance tests
replay matches of every tactical identity.

### Rejected files

Every rejection carries a `ReplayErrorCode` and a message naming the field by its
path, for example `initialState.players[3].position.x: expected a number`.

| Code                        | Cause                                                                  |
|-----------------------------|------------------------------------------------------------------------|
| `kIoError`                  | the file cannot be read or written                                     |
| `kMalformed`                | not JSON, a missing or mistyped field, commands out of order           |
| `kUnsupportedSchemaVersion` | a `schemaVersion` other than 18                                        |
| `kIncompatibleCoreVersion`  | recorded with another `coreVersion`                                    |
| `kInvalidSetup`             | an invalid initial state, command or checkpoint list                   |
| `kSimulationFailed`         | a step of the playback failed                                          |
| `kCheckpointMismatch`       | playback does not reproduce a recorded hash                            |

A replay is valid only for the core version that recorded it. A change to
simulation behavior bumps `coreVersion` and makes older replays unplayable on
purpose, instead of letting them play back into a different match.

### The seed is a string on purpose

`seed` is an unsigned 64-bit value written as a decimal JSON string, for example
`"18446744073709551615"` for `UINT64_MAX`. Many JSON parsers decode numbers as
IEEE 754 doubles, exact only up to 2^53; a larger seed would come back rounded,
and a rounded seed is a different simulation. State hashes are strings for the
same reason.

Consumers keep the string or parse it as an unsigned 64-bit integer, never
through a floating-point type. Producers keep the quotes: removing them looks
like a cleanup and invalidates every recorded replay. `tests/cli/smoke.cmake`
asserts that `seed` is a string and feeds `UINT64_MAX` back through the CLI.

Doubles are written in their shortest round-trippable form and read back bit for
bit, so an initial state survives the file unchanged.

### Versioning

`schemaVersion` is a single integer that counts up by one. Raise it when a
reader of the previous format would misread a new file:

- a field is removed or renamed,
- a field changes its JSON type,
- a field changes its meaning, unit, or encoding.

Otherwise leave it. Key order, indentation and line endings are not part of the
contract. A field the simulation needs to reproduce a match — a new
configuration parameter or state field — is required, since a reader cannot
guess it; such an addition comes with a new `coreVersion`.

Consumers read `schemaVersion` first and reject an unknown version with an
error naming it: a misread layout yields a run that looks valid and reproduces
nothing. Producers write only the current version. A change that raises it
updates this chapter, `replayJson.cpp` and its tests in the same commit, and
records below whether older files stay readable.

`schemaVersion` describes the file, `coreVersion` the simulation that produced
it. A change to simulation behavior — a different random mapping, a different
update order — invalidates replays without changing the format, so it raises
`coreVersion`, not `schemaVersion`.

From version 3 on, a reader rejects every older version with a message to
record the scenario again with the same seed.

| Version | Change                                                                                     |
|---------|--------------------------------------------------------------------------------------------|
| 1       | Initial schema: metadata only (`coreVersion`, `createdAt`, `seed`, `gameTime`).            |
| 2       | A playable replay: adds `config`, `initialState`, `commands` and `checkpoints`; `gameTime` is the final tick. Version 1 files cannot be played back and are rejected. |
| 3       | Checkpoints add `eventHash`. |
| 4       | Tactics carry their `contentHash` next to the tactic, `changeTactic` commands, and `checkpointIntervalTicks`. |
| 5       | The ball gains `height`, `verticalVelocity` and `spin`, the ball configuration its flight and bounce constants, and reception its `controlHeight`. |
| 6       | Players gain `shotTechnique`, `strongFoot` and `weakFootAccuracy`, the state its score and last goal, and the configuration its `shooting` and `woodwork` blocks ([shooting](on-ball.md#shooting)). |
| 7       | Players gain `keeperPositioning` and `keeperAnticipation`, reception its `handsRadius` and `handsHeight`, and the configuration its `goalkeeper` block ([goalkeeper](goalkeeper.md#positioning-and-sweeping)); tactics are files of tactic format version 2. |
| 8       | Players gain `keeperReflexes` and `keeperHandling`, and the configuration its `shotStopping` block ([shot stopping](goalkeeper.md#shot-stopping)). |
| 9       | Players gain `jumping`, `heading` and `strength`, and the configuration its `aerial` block ([aerial duels](duels.md#aerial-duels-and-headers)). |
| 10      | The ball gains `held`, passing its `maxLoftedSpeed` and `loftedErrorFactor`, the pass scoring its `loftedBias`, and the decisions their `distribution` block ([goalkeeper distribution](goalkeeper.md#distribution)); tactics are files of tactic format version 3. |
| 11      | The decisions gain `stuckSeconds`, `clearTouchlineMargin` and `clearGoalLineMargin` ([pass decisions](on-ball.md#without-a-valid-option)), the defensive configuration `idleSeconds` and `idlePressRadius` ([pressing](team-tactics.md#a-carrier-who-stands-on-the-ball)). |
| 12      | Players gain `ballControl` and `dribbling`, the ball `carryTouch`, the ball physics `minCarrySpeedFactor` and `maxCarrySpeedFactor`, and the decisions their `carrying` block ([ball carrying](on-ball.md#ball-carrying)). |
| 13      | The configuration's `challenge` block becomes its `duels` block ([duels](duels.md#duels-on-the-ball)). |
| 14      | Players gain `decisionMaking`, and the decisions `skillTemperatureSpread` and `maxMisjudgement` ([decision-making](on-ball.md#decision-making)). |
| 15      | The ball gains `sideSpin`, the ball physics `magnusCurvature`, the shot scoring `curledSpeed`, `chippedSpeed`, `curlSpin`, `spinError`, `speedError` and `maxLaunchSlope`, and the shooting `chipBackspin` ([shot types](on-ball.md#shot-types)). |
| 16      | The shooting gains `lungeRadius` and `lungeHeight` ([tackles and blocks](duels.md#tackles-and-blocks)). |
| 17      | The decisions gain their `crossing` block ([crossing](on-ball.md#crossing)). |
| 18      | Tactics are files of tactic format version 4, which may hold eleven slots ([tactics](tactics.md#tactic-file-format)). |
| 19      | Players gain `fitness` (`sprintReserve`, `playedTicks`, `topSpeedShare`), and the configuration its `clock` and `fitness` blocks ([match clock](match-clock.md), [fitness](players.md#fitness)). |
| 20      | The configuration gains its `offside` block and the clock its `freeKickLostSeconds` ([offside](match-engine.md#offside)). |
| 21      | Players gain `yellowCards` and `sentOff`, the configuration its `fouls` block and the clock `penaltyLostSeconds` and `cardLostSeconds` ([fouls](fouls.md)). |
| 22      | The configuration gains its `setPieces` block, restarts are on by default, and tactics carry their set-piece plans in `setPieces` ([set pieces](set-pieces.md)). |
| 23      | The `setPieces` block gains `freeKickSeconds`, `penaltySeconds`, `freeKickRange`, `wallPlayers` and `wallSpacing`, and tactics may plan a `freeKick` and a `penalty` ([free kicks](set-pieces.md#free-kicks), [penalties](set-pieces.md#penalties)). |

## Decision trace

The decision trace answers "why did he do that, and why did it go wrong?" for a
recorded match. It replays the match with
[diagnostics](match-engine.md#decision-diagnostics) on, lists every decision of
the chosen players in the chosen ticks with its reasons, and follows every pass
to its outcome. Tracing changes nothing; playback verifies every checkpoint as
usual.

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
  with its aim and its goal, save and block chances -- a curled shot reads
  `curls a shot at`, a chip `chips at` ([shot types](on-ball.md#shot-types)) --
  ([shot decisions](on-ball.md#shot-decisions)), `ActionScores` for an action without
  the ball ([off-ball movement](team-tactics.md#off-ball-movement)). A goalkeeper
  `throws to` or `punts to` a teammate from his hands and `kicks long to` one
  from his feet, a long ball's utility with its `directness`
  ([goalkeeper distribution](goalkeeper.md#distribution)). A goalkeeper's call to
  come for a ball (`sweeps`) or not (`stays home`) gives both arrivals, his
  misjudgement and the head start he needed instead
  ([goalkeeper](goalkeeper.md#positioning-and-sweeping)).
- **Crosses** read `crosses to the <area> (x, y) h m high for #n`, with the
  parts of `crossContributions()`, the win chance and the keeper's claim
  ([crossing](on-ball.md#crossing)), and are followed to their outcome like
  passes -- a `decision` failure if the win chance was at most
  `1 − riskyDecision`. A pass or a cross won in the air, a long ball's too,
  was received by a teammate who won it, intercepted by an opponent.
- **Misjudged estimates**: a poor decider's judged interception risk, goal,
  save or block chance follows the estimate it differs from, as in
  `estimated risk 0.64, judged 0.31` ([decision-making](on-ball.md#decision-making)).
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
| `judgement` | he saw him and estimated the pass's interception risk at least `riskyDecision` (0.5), but as a poor decider [misjudged](on-ball.md#decision-making) it below |
| `decision` | he saw him and still chose a pass whose interception risk, as he judged it, was at least `riskyDecision` |
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

`DecisionTracer` works on any simulation with diagnostics on, not only a
playback; its entries hold the full diagnostics for tools.

### Cost

Diagnostics are built only for the filtered players in the filtered ticks; the
rest of the match runs at full speed. With diagnostics off — every normal run
— a decision pays one pointer check.

## Debug viewer

The debug viewer shows a match in 2D: the pitch, both teams, the ball and who
has it, and for a selected player his target, what he sees and remembers, and
how he decided his last pass. It is a static web page (`apps/sim-viewer`,
TypeScript and Canvas) that plays back **debug frames** written by `sim-cli`.

The viewer never simulates. `sim-cli` runs the match headlessly and writes one
frame per tick; the page only reads the file. Pausing, stepping, scrubbing,
speed and selection change the screen, never the match.

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

Then open the printed URL, `http://localhost:8080/?frames=frames.json`. Without
a file argument, `pnpm run serve` serves the page alone, and **Frames** loads a
file from disk. Browsers do not load JavaScript modules from `file://`, so the
page needs a server: `scripts/serve.mjs`, a few lines of `node:http` on
`127.0.0.1`; `--port <n>` changes the port.

Collecting frames does not change the match: replay and final state hash are the
same with or without `--frames-out`. Every frame carries its tick's state hash,
so it can be matched against a replay checkpoint.

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

`sim-cli --frames-out` writes one JSON document: read-only output for the
viewer, versioned apart from the [replay format](#replay-format). Nothing reads
it back into a simulation; a replay reproduces a match. Positions, velocities
and scores are rounded to three decimals, which keeps a 30-second match of
fourteen players around 17 MB. `--frames-out` records at most 3600 ticks (two
minutes at 30 Hz, `kMaxFrameTicks`): the recording stays in memory until
written, and a whole match would run to gigabytes. The viewer is for
situations; a replay records any length. An abridged example with illustrative
values:

```json
{
  "format": "elyverse-debug-frames",
  "version": 4,
  "coreVersion": "0.34.0",
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
        "height": 0, "verticalVelocity": 0, "spin": 0, "sideSpin": 0,
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
              "receiverConfidence": 1, "interceptionRisk": 0.02, "judgedInterceptionRisk": 0.02, "completion": 0.98,
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
  does not know by its name. A decision's `outcome` is `passed`, `shot`, `held`,
  `turned`, `carried`, `dribbled`, `crossed` or `noValidOption`, `chosen` indexes
  `candidates` (null for a shot or a cross), and `rejection` is `valid` or
  the reason a candidate cannot be played. A candidate's `completion`, `utility` and
  `rejection` follow its `judgedInterceptionRisk`, which differs from its
  `interceptionRisk` only for a poor decider ([decision-making](on-ball.md#decision-making)).

A change that breaks existing readers raises `version`; the viewer rejects
versions it does not know. New events and fields need no new version: the
viewer shows an unknown event by its name and ignores an unknown field.

| Version | Change |
|---------|--------|
| 2 | `zones`, `pitchControl`, the teams' `tactic`, `instruction` and `press` |
| 3 | the ball's `height`, `verticalVelocity` and `spin` ([ball movement](ball.md#ball-movement)) |
| 4 | the decision outcome `shot` and the `shotAttempted` event ([shot decisions](on-ball.md#shot-decisions)) |
| 5 | every frame's `score`; `struckAt` and `struckHeight` in `shotAttempted`; the events `shotDeflected`, `shotHitWoodwork`, `shotResolved` and `goalScored` ([shooting](on-ball.md#shooting)) |

Added without a new version: the events `saveAttempted`
([shot stopping](goalkeeper.md#shot-stopping)) and `aerialContest`
([aerial duels](duels.md#aerial-duels-and-headers)); the ball's `held`, the
`lofted` of the pending pass and of a pass candidate, and a decision's `held`
([goalkeeper distribution](goalkeeper.md#distribution)); a player's `look`, a
decision's `reason` and `target`, and an action's `idlePress`
([pass decisions](on-ball.md#without-a-valid-option)); a pass candidate's
`judgedInterceptionRisk` ([decision-making](on-ball.md#decision-making)); the
ball's `sideSpin` and the `shotType` of `shotAttempted`
([shot types](on-ball.md#shot-types)); a pending pass's `cross`, `height` and
`curvature`; the event `tackleAttempted`
([tackles and blocks](duels.md#tackles-and-blocks)); the event `crossAttempted`
([crossing](on-ball.md#crossing)); every frame's `clock` (as `formatClock()`
shows it), `period` and `endsChanged`, and the event `periodEnded`
([match clock](match-clock.md)); the event `offsideCalled`
([offside](match-engine.md#offside)); the event `setPieceTaken`
([set pieces](set-pieces.md)); the event `foulCalled` and every
player's `yellowCards` and `sentOff` ([fouls](fouls.md)). From the second half on the viewer turns the
pitch round the centre spot, as the teams have changed ends. Without a `look` the
viewer draws the vision cone along the facing.

### Development

`apps/sim-viewer` depends only on TypeScript, pinned exactly. The committed
`pnpm-lock.yaml` makes an install with `--frozen-lockfile` (CI and
`make viewer-test`) resolve to that version. Corepack provides pnpm from the
`packageManager` field. Behind a proxy that blocks `registry.npmjs.org`, set
`registry=<url>` in an `.npmrc` to use a mirror.

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

`libs/sim-analytics` turns a match's
[domain events](match-engine.md#match-events-and-decision-diagnostics) into
statistics. It is read-only: it runs no system, writes no state and reads no
state beyond the match context. Everything it measures is in the event stream,
so a match can be analysed live, from recorded events, or by replaying it.

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
| `goals` | `GoalScored` | goals scored, own goals of the opponent included: the side's score |
| `offsides` | `OffsideCalled` | offsides the side's players were caught in ([offside](match-engine.md#offside)) |
| `fouls`, `yellowCards`, `redCards` | `FoulCalled` | fouls the side's players committed and the cards they were shown, a second yellow counted as red ([fouls](fouls.md)) |
| `corners`, `throwIns`, `goalKicks` | `SetPieceTaken` | the set pieces the side took ([set pieces](set-pieces.md)) |
| `points` | `GoalScored`, `PeriodEnded` | 3 for a win, 1 for a draw, 0 for a defeat once the second half has ended ([full time](match-clock.md#full-time)); empty before |
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

`toStatsJson()` writes format `elyverse-match-stats`, version 2 — version 1 had
no `finished`, `score`, `goals` or `points`:

```json
{
  "format": "elyverse-match-stats",
  "version": 2,
  "ticks": 1800,
  "seconds": 60.0,
  "finished": false,
  "score": { "home": 1, "away": 0 },
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

No shot outcomes, expected goals or player ratings yet: goals are counted, but
no other shot outcomes. Pressures are team presses, not every individual pressing action.

## sim-benchmark

`apps/sim-benchmark` plays a series of matches between two tactic files of the
same size -- seven-a-side on the sandbox, eleven-a-side on the standard pitch -- and writes their statistics, with confidence intervals: how each style
fares against another. It is also the simulation's performance check.

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
| `--minutes`                   | 10      | the match duration in real minutes ([match clock](match-clock.md)), at most 1440; every match is played to full time |
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

The results file, format `elyverse-style-benchmark`, version 2 — version 1 had a
fixed number of `ticks` per match instead of `minutes`, and no `outcomes` —
holds the
styles, each style's `styleSummary` over all its matches — home and away —
the `separations`, and every pairing's summaries and matches. A **separation**
is a metric on which one style's 95 % interval lies wholly above another's: a
difference beyond chance. The console lists them.

`--verify-replays n` records the first `n` matches of every series again as a
replay, plays it back and fails the run if a checkpoint differs: the sampled
determinism check.

### The matches

Every match is the [tactic match](scenarios.md#the-tactic-match): the kickoff
fixture with the two tactics, the standard systems,
[restarts](match-engine.md#restarts) (a ball waiting on the line would blur
every statistic), [offside](match-engine.md#offside) and [fouls](fouls.md) on. Match `i` plays with seed `matchSeed(base, i)`, SplitMix64 of
the base seed advanced by `i + 1` steps: neighbouring matches get unrelated
seeds, and a seed depends only on base seed and index, not on how many matches
run or in which order. `sim-cli --home-tactic ... --away-tactic ... --seed <its
seed> --duration <minutes>` replays a match's tactics, seed and duration, but
`sim-cli` has no offside or foul option, so it diverges at the first offside
or foul.

Statistics come from [match analytics](#match-analytics) over each match's
events. A match fails the whole run — naming its index, seed and tick — if a
step fails or produces a non-finite value, a statistic is not a finite number,
or it has not reached full time by `maxMatchTicks()`, four times both halves
with the most added time: a bound no match that plays on gets near.

### Results

The results file is JSON, format `elyverse-benchmark`, version 2 — version 1
had `ticks` instead of `minutes`, no `outcomes`, and stats of version 1 —: the
core version, both tactics by name and content hash, the series (`matches`,
`baseSeed`, `minutes`, `restarts`, `offside`, `fouls`), the `outcomes` — `homeWins`, `draws` and
`awayWins` —, a
`summary` per side and metric, `goals` and `points` among them, and `results`,
every match's index, seed and statistics with its score. A round robin adds the
`outcomes` to every pairing, and each style's `points` per match to its
`styleSummary`.

A metric's summary holds its `count` of matches — metrics a match leaves empty,
such as a PPDA without defensive actions, are left out — its `mean`, sample
`variance` and `ci95`, the 95 % confidence interval of the mean,
`mean ± t · s / √n` with Student's t (`seriesSummary.hpp`).

The file holds no timing, and every sum runs in match order, so the same
tactics, series and core version always give **the same bytes**, whatever
`--jobs` is; a test and the smoke test hold that.

The console shows each match's seed and wall-clock time, the headline metrics
of both sides with their intervals, and the mean and slowest match time.

### P2 acceptance

**In CI**, the `style-benchmark` job builds the `ci-release` preset and runs
`ctest -L benchmark`: `tests/benchmark/p2StyleBenchmark.cmake` plays the round
robin of the three [tactical identities](tactics.md#tactical-identities),
eleven a side on the standard pitch, twenty six-minute matches per pairing —
played to full time, so with half time and added time — with seed 1 and one
sampled replay per pairing. It must finish without an invalid
value within 5 s per match and 450 s for all 180 together — the sum of their
times, not the elapsed time (a sum of about 175 s locally at the time of
writing), every sampled replay must reproduce, and the
styles must separate where their identities promise:

| Metric                  | Separation                              |
|-------------------------|-----------------------------------------|
| `regains`               | pressing above possession and counter   |
| `regainsAttackingThird` | pressing above counter                  |
| `ppda`                  | counter above pressing and possession — the deep block allows the most passes per defensive action |

These hold for base seeds 1 to 5. **Since the identities play eleven a side**
the styles separate far more clearly than on the seven-a-side sandbox, where
thirty matches per pairing were needed for pressing's attacking-third regains
over counter's: on the full pitch twenty suffice for every separation above,
and counter's PPDA now also separates from possession's. Two separations are
not asked for because twenty matches do not show them for every seed:
possession's PPDA above pressing's (seed 4 misses it) and pressing's
attacking-third regains above possession's (seed 5).

Two guardrails also apply:

- **Shots.** Every side of every pairing shoots between 0.25 and 40 times per
  six-minute match on average, so a change cannot stop the shooting or turn the
  pitch into a shooting gallery. At the time of writing the range is 2.3 to
  9.4 -- 2.15 to 9.65 over seeds 1 to 5 -- on the full pitch, since matches
  run to full time with their added time; counter at home against pressing the
  fewest
  ([shot decisions](on-ball.md#shot-decisions)).
- **Ownership.** No player owns the ball longer than 20 s at a time
  (`longestOwnershipSeconds`, [match analytics](#match-analytics)). A carrier
  without an option once kept it for 318 s; now he
  [looks round](on-ball.md#without-a-valid-option), a carrier standing on the
  ball [is gone at](team-tactics.md#a-carrier-who-stands-on-the-ball), and a
  carrier who carries it about
  [gets stuck](on-ball.md#holding-on-and-getting-stuck); the longest was
  10.4 s seven a side. Eleven a side, on the longer pitch, it was 14.3 s with
  seed 1 and 17.4 s at worst over seeds 1 to 5, so the bound rose from 15 s;
  since attackers keep to the [offside line](match-engine.md#offside) it is
  13.3 s with seed 1 and 17.5 s at worst.

The test is registered for Release builds only; in Debug it would take minutes.

**The full run** is twenty 90-minute matches per pairing, the command above.
Its results at the time of writing (core 0.34.0, eleven a side) are in
[tactical identities](tactics.md#over-whole-matches).

### P2 performance budget

Since the P2 tactics play eleven a side, a 90-minute match has the budget of
the [eleven-a-side match](match-engine.md#performance): under 45 s, which the
Release-only `eleven-a-side-budget` test checks. A 90-minute seven-a-side match
of the seven-a-side presets took about 5.8 s (2-core cloud container, GCC 13).

Debug builds are around ten times slower; the smoke test therefore plays two
one-minute matches only.
