# Tactics

The tactic data model, its file format, and the three tactical identities the
data files define.

- [Tactic model](#tactic-model)
- [Tactic file format](#tactic-file-format)
- [Tactical identities](#tactical-identities)

## Tactic model

A tactic describes how a team wants to play, independent of any match: where
its players stand, what each of them is responsible for, and how the team
behaves in each phase of the game. The `sim-tactics` library
(`ElyverseFootball::sim-tactics`) holds the data model; it depends on `sim-core`
only, and `sim-match` reads tactics without `sim-tactics` knowing anything
about a match. See the [implementation plan](implementation-plan.md), section 7,
and the [game design document](game-design-document.md), section 8.

```text
Tactic
├─ name, description
├─ slots[7]                      base shape + responsibilities
│   ├─ position { depth, width }
│   └─ responsibilities[] { responsibility, weight }
├─ principles                    what holds in every phase
│   ├─ pressingLine
│   ├─ pressingTriggers[]
│   ├─ positioning { targetDistance, spacing, pressure, occupancy, transitionRisk }
│   └─ goalkeeper { sweeping, directness }
└─ phases[phase]                 one PhaseInstruction per tactical phase
    └─ lineHeight, blockLength, blockWidth, ballShift,
       pressingIntensity, passingRisk, runFrequency
```

The description is free text for people -- what the tactic is for and which
parameters express it; nothing in the match reads it.

`Tactic::create(TacticSpec)` is the only way to get a `Tactic`, so every tactic
that exists is valid. It reports every rule a spec breaks, each with a code, the
path of the offending field as a tactic file spells it
(`slots[3].position.depth`, `phases.pressing.lineHeight`) and a message. A
`Tactic` is immutable; a team changes tactics by getting another one.

`referenceTacticSpec()` (`referenceTactic.hpp`) is a neutral, valid tactic for
tests and hand-built fixtures. Tactics are data: tactic files under
`data/tactics/` describe them in the [tactic file format](#tactic-file-format).

### Units and conventions

Everything is a fraction of the pitch, so one tactic fits any pitch size.

- **Depth** runs from the team's own goal line (0) to the opponent's (1). A
  shape reads the same for both teams, whichever way they attack.
- **Width** runs from the touchline at `y = 0` (0) to the one at `y = width` (1)
  in [pitch coordinates](match-engine.md#match-geometry), for both teams alike: a slot at
  width 0.1 plays near the same touchline for home and away.
- **Heights and lengths** in phase instructions are fractions of the pitch
  length, **widths** fractions of the pitch width.
- **Dials** (intensity, risk, frequency, shift) are dimensionless, in `[0, 1]`.

A tactic has exactly seven slots: P2 is seven-a-side, eleven-a-side arrives with
the P3 baseline. The player in slot `i` is the `i`-th player of his side in the
match, in the order the match state lists them (his place in the line-up).

### Base shape

Each slot has a `position` in the base shape: where the player stands relative
to his teammates. The base shape gives the arrangement; the phase instructions
give its height and compactness in the match.

| Field   | Range    | Meaning                                         |
|---------|----------|-------------------------------------------------|
| `depth` | `[0, 1]` | from the own goal line (0) to the opponent's (1) |
| `width` | `[0, 1]` | from the touchline at `y = 0` to the one at `y = width` |

### Responsibilities

A slot holds a list of atomic responsibilities, each with a weight in `(0, 1]`
that scales its pull on the player's decisions; 1 is a primary duty. A slot may
hold none: its player simply keeps his place in the shape.

| Responsibility      | Duty                                                        |
|---------------------|-------------------------------------------------------------|
| `guardGoal`         | stay between the ball and the own goal, near the goal line   |
| `holdDefensiveLine` | form the last outfield line and move with it                 |
| `holdRestDefence`   | stay behind the ball when the team attacks                    |
| `provideWidth`      | hold the wing lane near the touchline                         |
| `occupyHalfspace`   | stand in the lane between wing and centre                     |
| `supportCarrier`    | offer the ball carrier a short, open passing option           |
| `runInBehind`       | attack the space behind the opponent's defensive line         |
| `markOpponent`      | stay close to an opponent in the own zone                     |
| `cover`             | protect the space behind a teammate who steps out             |
| `closePressingLine` | join the press and close the carrier's passing lanes          |

A slot must not hold contradicting duties, which pull the same player to
different places:

- `provideWidth` and `occupyHalfspace`,
- `runInBehind` and `holdRestDefence`, `runInBehind` and `holdDefensiveLine`,
- `guardGoal` and any other responsibility.

At most one slot guards the goal.

#### Role presets

Familiar roles are presets (`rolePreset.hpp`) that fill a slot's
responsibilities. The tactic keeps only the responsibilities, so the match never
knows which preset a slot came from, and a new role needs no new code.

| Preset              | Responsibilities (weight)                                          |
|---------------------|--------------------------------------------------------------------|
| `goalkeeper`        | guardGoal (1)                                                      |
| `centreBack`        | holdDefensiveLine (1), markOpponent (0.6), cover (0.6)             |
| `fullBack`          | holdDefensiveLine (1), provideWidth (0.6), markOpponent (0.5)      |
| `holdingMidfielder` | holdRestDefence (1), cover (0.8), supportCarrier (0.5)             |
| `centralMidfielder` | supportCarrier (1), closePressingLine (0.6), occupyHalfspace (0.4) |
| `winger`            | provideWidth (1), runInBehind (0.5), closePressingLine (0.5)       |
| `insideForward`     | occupyHalfspace (1), runInBehind (0.7), closePressingLine (0.6)    |
| `striker`           | runInBehind (1), closePressingLine (1), supportCarrier (0.4)       |

### Principles

| Field              | Range      | Meaning                                                  |
|--------------------|------------|----------------------------------------------------------|
| `pressingLine`     | `[0, 1]`   | how far up the pitch, from the own goal line, the ball must be before a team without it is in the pressing phase |
| `pressingTriggers` | list       | the situations that start a press, each at most once      |
| `positioning.*`    | `[0, 10]`  | weights of the positioning cost components               |
| `goalkeeper.sweeping` | `[0, 1]` | how readily the keeper comes off his line for a ball played in behind ([goalkeeper](goalkeeper.md#positioning-and-sweeping)) |
| `goalkeeper.directness` | `[0, 1]` | how readily the keeper goes long rather than playing out ([goalkeeper distribution](goalkeeper.md#distribution)) |

Pressing triggers (GDD section 8.7): `poorFirstTouch`, `backPass`,
`receiverFacingOwnGoal`, `isolatedReceiver`, `slowPass`.

The positioning weights (`targetDistance`, `spacing`, `pressure`, `occupancy`,
`transitionRisk`) weigh the components of the desired-region cost of the
implementation plan, section 7.2, against each other.

### Phases

A team is always in one of seven phases, four with the ball and three without.
The tactic has one instruction per phase.

| Phase                 | With the ball | Situation                                   |
|-----------------------|---------------|---------------------------------------------|
| `buildUp`             | yes           | the ball in the own third                   |
| `progression`         | yes           | the ball in the middle third                |
| `finalThird`          | yes           | the ball in the opponent's third            |
| `attackingTransition` | yes           | just won the ball                           |
| `defensiveBlock`      | no            | defending in a block                        |
| `pressing`            | no            | the ball high enough up the pitch to press  |
| `defensiveTransition` | no            | just lost the ball                          |

| Instruction         | Range     | Meaning                                                     |
|---------------------|-----------|-------------------------------------------------------------|
| `lineHeight`        | `[0, 1]`  | distance of the defensive line from the own goal line       |
| `blockLength`       | `(0, 1]`  | length the outfield block spans: vertical compactness       |
| `blockWidth`        | `(0, 1]`  | width the outfield block spans: horizontal compactness      |
| `ballShift`         | `[0, 1]`  | how far the block follows the ball, 0 holds, 1 centres on it |
| `pressingIntensity` | `[0, 1]`  | how eagerly and with how many players the team presses      |
| `passingRisk`       | `[0, 1]`  | how much risk the player on the ball accepts going forward, with a pass or a [shot](on-ball.md#shot-decisions) |
| `runFrequency`      | `[0, 1]`  | how often players run into space instead of holding position |

`lineHeight + blockLength` must not exceed 1: such a block would reach past the
opponent's goal line.

### Validation

| Code                             | Cause                                                  |
|----------------------------------|--------------------------------------------------------|
| `kEmptyName`                     | the tactic has no name                                 |
| `kWrongSlotCount`                | not exactly seven slots                                |
| `kSlotOutsidePitch`              | a shape position outside `[0, 1]`, or NaN              |
| `kValueOutOfRange`               | a principle or instruction outside its range, or NaN   |
| `kUnknownResponsibility`         | a value outside the declared responsibilities          |
| `kDuplicateResponsibility`       | a slot lists a responsibility twice                    |
| `kInvalidResponsibilityWeight`   | a weight outside `(0, 1]`                              |
| `kContradictoryResponsibilities` | a slot holds two contradicting duties                  |
| `kTooManyGoalkeepers`            | a second slot guards the goal                          |
| `kUnknownPressingTrigger`        | a value outside the declared triggers                  |
| `kDuplicatePressingTrigger`      | a trigger listed twice                                 |
| `kContradictoryParameters`       | values fine alone that together describe no block      |

Errors come in a fixed order: name, slots by index, principles, phases in the
order of the table above.

## Tactic file format

Tactics are data: tactic files under `data/tactics/` describe them, so a
tactical identity can be tuned without recompiling. The `sim-tactics` library
reads and writes them (`tacticJson.hpp`); the model they describe is in
[tactics.md](#tactic-model).

### Version 2

```json
{
  "format": "elyverse-tactic",
  "version": 3,
  "name": "reference",
  "description": "A neutral 1-2-1-3 for tests and hand-built fixtures.",
  "slots": [
    { "position": { "depth": 0.04, "width": 0.5 }, "role": "goalkeeper" },
    {
      "position": { "depth": 0.2, "width": 0.3 },
      "responsibilities": [
        { "responsibility": "holdDefensiveLine", "weight": 1.0 },
        { "responsibility": "markOpponent", "weight": 0.6 }
      ]
    }
  ],
  "principles": {
    "pressingLine": 0.6,
    "pressingTriggers": ["backPass", "receiverFacingOwnGoal"],
    "positioning": {
      "targetDistance": 1.0,
      "spacing": 1.0,
      "pressure": 0.5,
      "occupancy": 0.5,
      "transitionRisk": 0.5
    },
    "goalkeeper": { "sweeping": 0.5, "directness": 0.5 }
  },
  "phases": {
    "buildUp": {
      "lineHeight": 0.35,
      "blockLength": 0.45,
      "blockWidth": 0.85,
      "ballShift": 0.4,
      "pressingIntensity": 0.3,
      "passingRisk": 0.5,
      "runFrequency": 0.3
    }
  }
}
```

The example is shortened: a file has seven slots and an entry for each of the
seven phases.

| Field              | Type   | Meaning                                                     |
|--------------------|--------|-------------------------------------------------------------|
| `format`           | string | always `"elyverse-tactic"`                                   |
| `version`          | integer| the format version, 3                                        |
| `name`             | string | the tactic's name; a file is named after it (`<name>.json`)  |
| `description`      | string | optional; what the tactic is for and which parameters express it |
| `slots`            | array  | seven slots, in line-up order                                |
| `slots[].position` | object | `depth` and `width`, fractions of the pitch                  |
| `slots[].role`     | string | a role preset, expanded into its responsibilities on load    |
| `slots[].responsibilities` | array | `{ "responsibility", "weight" }` entries          |
| `principles`       | object | `pressingLine`, `pressingTriggers`, `positioning` weights, `goalkeeper.sweeping` and `goalkeeper.directness` |
| `phases`           | object | one instruction per phase, keyed by phase name               |

Every field is required except `description`. A slot has either `role` or
`responsibilities`, not both: a preset plus extra duties would hide what the
slot really holds. Names are spelled as in [tactics.md](#tactic-model): phases
(`buildUp`, `progression`, ...), responsibilities (`provideWidth`, ...), role
presets (`winger`, ...) and pressing triggers (`backPass`, ...). Values and
their ranges are the model's.

### Loading

`loadTactic(path)` and `parseTacticJson(text, source)` produce the same
validated `Tactic` that `Tactic::create()` builds in code and apply all of its
rules. They reject:

| Code                  | Cause                                                          |
|-----------------------|----------------------------------------------------------------|
| `kIoError`            | the file cannot be read                                        |
| `kMalformed`          | not JSON, another `format`, a missing, mistyped or unknown field, an unknown name |
| `kUnsupportedVersion` | a `version` other than 3                                        |
| `kInvalidTactic`      | the tactic breaks a rule of the model; every broken rule is listed |

Every message names the file and the offending field:

```text
data/tactics/pressing.json: phases.pressing.lineHeight: 1.3 must lie in [0, 1]
```

Unknown fields are errors rather than ignored, so a misspelled key cannot
silently fall back to nothing.

Loading is deterministic: the same file always yields an equal tactic.
`contentHash()` (`tacticHash.hpp`) hashes the loaded model, not the text, so
whitespace, key order or a role written out as its responsibilities do not
change it; replays record it to name the tactic a team played.
`toTacticJson()` writes a tactic back, with responsibilities written out, and
reading that text yields an equal tactic.

### Versioning

`version` counts up by one when a reader written against this chapter would
misread a new file: a field removed, renamed, or changed in type or meaning.
Readers accept exactly the version they know and reject every other one with
an error naming it. A new optional field with a documented default does not
need a new version; a new required one does.

| Version | Change          |
|---------|-----------------|
| 1       | Initial format. |
| 2       | Adds the required `principles.goalkeeper.sweeping` ([goalkeeper](goalkeeper.md#positioning-and-sweeping)). |
| 3       | Adds the required `principles.goalkeeper.directness` ([goalkeeper distribution](goalkeeper.md#distribution)). |

### Files

| File                          | Tactic                                               |
|-------------------------------|------------------------------------------------------|
| `data/tactics/reference.json` | the neutral reference tactic of `referenceTacticSpec()` |
| `data/tactics/possession.json` | keeps the ball, counterpresses ([tactical identities](#tactical-identities)) |
| `data/tactics/counter.json`   | deep block, vertical after a regain                  |
| `data/tactics/pressing.json`  | high line, presses on every trigger                  |

`tests/unit/sim-tactics/tacticFilesTests.cpp` loads every file in
`data/tactics/`, checks that each is named after its tactic, and that
`reference.json` equals the reference tactic built in code.

## Tactical identities

Three tactics under `data/tactics/` give the sandbox recognisable styles:
`possession`, `counter` and `pressing`. They use the same seven-a-side tactic
model as `reference.json` (see [tactics](#tactic-model) and the
[file format](#tactic-file-format)) and differ **only in data**: no system knows
their names, and a new style is a new file.

### Intent and parameters

| | possession | counter | pressing |
|---|---|---|---|
| Intent | keep the ball, circulate safely, counterpress hard on a sloppy loss | defend deep and compact, strike quickly and vertically after a regain | hunt the ball high, with several players, on every trigger |
| Shape | wide 1-2-1-3, wingers on the touchlines | narrow 1-2-1-2-1 with inside forwards | high 1-2-1-3 with a central midfielder |
| `pressingLine` | 0.55 | 0.85 (rarely presses) | 0.4 (presses early) |
| Pressing triggers | poor first touch, back pass | none | all five |
| `lineHeight` out of possession | 0.35–0.4 | 0.15 | 0.4–0.45 |
| `pressingIntensity` | 0.4, 1.0 in defensive transition (counterpress) | 0.1–0.25 | 1.0 |
| `passingRisk` | 0.2–0.35 | 0.6, 0.9 in attacking transition | 0.5–0.75 |
| `runFrequency` | 0.2–0.35 | 0.5–0.9 | 0.3–0.7 |
| `blockWidth` in possession | 0.9–0.95 | 0.8 | 0.85 |

The positioning weights follow the same intent: possession values spacing
and occupancy and guards against transitions (`transitionRisk` 0.8), counter
cares least about occupancy and transition risk and most about reaching its
targets.

`passingRisk` reaches the player on the ball through `scoringForRisk()`
([pass decisions](on-ball.md#pass-decisions)): the higher it is, the more a pass is
worth for the ground it gains and the less its interception risk counts.

### Playing them against each other

Any two tactics that fit seven a side meet in the tactic match
([scenarios](scenarios.md#scenario-catalogue)):

```sh
./build/debug/apps/sim-cli/sim-cli --home-tactic data/tactics/pressing.json \
  --away-tactic data/tactics/counter.json --seed 3 --ticks 1800 --frames-out frames.json
```

In code, `makeTacticMatch({.home = ..., .away = ...}, seed)` builds the same
setup.

### Signatures

`tests/acceptance/tacticalIdentityTests.cpp` checks what makes each style
recognisable, over fixed seeds:

- **Pressing wins the ball back higher.** Over sixty one-minute tactic
  matches against the reference tactic, pressing regains the ball more often
  than counter and at least one and a half times as often in the opponent's
  half (at the time of writing 315 regains, 139 of them high, against 192 and
  58). Twenty matches were too few: with the goalkeeper's own model they
  fell below the ratio by chance while sixty kept it.
- **Counter plays forward after a regain.** A hand-placed moment: home's
  holding midfielder has just won the ball in midfield, the striker is ahead
  of him with a defender near the lane, the centre backs behind him are free.
  In its attacking transition, counter's first action -- a pass, a carry or a
  dribble ([ball carrying](on-ball.md#ball-carrying)) -- goes more than five metres
  forward in at least eight more of twenty seeds than possession's does (at
  the time of writing 14 against 4).

`tacticFilesTests.cpp` additionally pins the direction in which the files
differ, such as pressing's lower pressing line and counter's deeper block,
so an edit cannot quietly flatten an identity.

### Over whole matches

The [style benchmark](tooling.md#a-round-robin-of-styles) plays every
pairing of the three identities. The full run — twenty 90-minute matches per
pairing, seed 1, restarts on, core 0.18.0, 180 matches — gives, per style over
its 120 matches home and away, mean and 95 % interval:

| Metric                  | possession            | counter              | pressing               |
|-------------------------|-----------------------|----------------------|------------------------|
| possession share        | 0.50 [0.47, 0.53]     | 0.44 [0.39, 0.48]    | **0.57** [0.55, 0.58]  |
| passes                  | 1052 [986, 1118]      | **626** [551, 701]   | 1396 [1341, 1450]      |
| pass completion         | 0.67 [0.66, 0.68]     | 0.68 [0.67, 0.70]    | **0.63** [0.62, 0.63]  |
| mean pass length (m)    | **12.4** [12.3, 12.5] | 12.0 [11.8, 12.1]    | 11.5 [11.4, 11.5]      |
| progressive passes      | 157 [144, 169]        | **77** [67, 87]      | 192 [180, 204]         |
| regains                 | 387 [357, 417]        | **228** [196, 261]   | **557** [531, 584]     |
| regains, attacking third| 89 [83, 95]           | 42 [37, 47]          | **179** [171, 188]     |
| presses                 | 379 [358, 400]        | **0**                | **1083** [1040, 1126]  |
| PPDA                    | 2.88 [2.80, 2.96]     | **3.95** [3.76, 4.13]| **2.36** [2.32, 2.40]  |
| pitch control share     | 0.52 [0.52, 0.53]     | **0.45** [0.44, 0.47]| 0.52 [0.51, 0.53]      |
| ball in attacking third | 0.29 [0.26, 0.33]     | 0.33 [0.28, 0.38]    | **0.40** [0.37, 0.43]  |

Every pair of styles separates — non-overlapping intervals — on at least
twelve metrics. What each identity shows:

- **Pressing** presses about three times as often as possession, wins the ball
  back most often and twice as often as possession in the attacking third,
  allows the fewest passes per defensive action, and — winning it high — has
  the most of the ball and the ball most in the attacking third. It pays with
  the lowest pass completion.
- **Counter** never presses, allows the most passes per defensive action,
  controls the least of the pitch and plays the fewest passes; the ball still
  spends as much time in its attacking third as in possession's.
- **Possession** plays the longest and a safe passing game, and presses
  moderately through its counterpress.

Two things are not there yet: possession does not own the most of the ball —
the pressing side wins it back so often that it has more — and counter's
directness shows in few, completed passes rather than in fast progression,
since players cannot dribble or play a through pass into a run. Both depend on
match mechanics beyond P2, not on the tactic files.

### What this is not

The styles are as distinct as the P2 systems let them be. Carriers pass to
where a teammate stands, not into his run, and cannot dribble or hold the ball
to wait for a runner, so a counter-attack is a sequence of forward passes
rather than a run with the ball.
