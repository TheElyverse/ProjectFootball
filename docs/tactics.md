# Tactics

The tactic data model, its file format, and the three tactical identities the
data files define.

- [Tactic model](#tactic-model)
- [Tactic file format](#tactic-file-format)
- [Tactical identities](#tactical-identities)

## Tactic model

A tactic describes how a team wants to play, apart from any match: where its
players stand, what each is responsible for, and how the team behaves in each
phase. The `sim-tactics` library (`ElyverseFootball::sim-tactics`) holds the
model. It depends only on `sim-core`; `sim-match` reads tactics, and
`sim-tactics` knows nothing about matches. See the
[implementation plan](implementation-plan.md), section 7, and the
[game design document](game-design-document.md), section 8.

```text
Tactic
├─ name, description
├─ slots[7 or 11]                base shape + responsibilities
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

The description is free text for people: what the tactic is for and which
parameters express it. Nothing in the match reads it.

`Tactic::create(TacticSpec)` is the only way to get a `Tactic`, so every tactic
is valid. It reports every rule a spec breaks, each with a code, the path of the
field as a tactic file spells it (`slots[3].position.depth`,
`phases.pressing.lineHeight`) and a message. A `Tactic` is immutable; a team
changes tactics by getting another one.

`referenceTacticSpec()` (`referenceTactic.hpp`) is a neutral, valid tactic for
tests and hand-built fixtures. Tactic files under `data/tactics/` describe
tactics in the [tactic file format](#tactic-file-format).

### Units and conventions

Everything is a fraction of the pitch, so one tactic fits any pitch size.

- **Depth** runs from the team's own goal line (0) to the opponent's (1), so a
  shape reads the same for both teams.
- **Width** runs from the touchline at `y = 0` (0) to the one at `y = width` (1)
  in [pitch coordinates](match-engine.md#match-geometry), for both teams: a slot
  at width 0.1 plays near the same touchline for home and away.
- **Heights and lengths** in phase instructions are fractions of the pitch
  length, **widths** fractions of its width.
- **Dials** (intensity, risk, frequency, shift) are dimensionless, in `[0, 1]`.

A tactic has seven slots, for seven-a-side on the sandbox — short scenarios —
or eleven, for eleven-a-side on a full-size pitch (`isSupportedSlotCount()`).
The player in slot `i` is the `i`-th player of his side in the match state
(his place in the line-up), and a tactic plays only for a side of its size.

### Base shape

Each slot has a `position` in the base shape: where the player stands relative
to his teammates. The phase instructions set the shape's height and compactness.

| Field   | Range    | Meaning                                         |
|---------|----------|-------------------------------------------------|
| `depth` | `[0, 1]` | from the own goal line (0) to the opponent's (1) |
| `width` | `[0, 1]` | from the touchline at `y = 0` to the one at `y = width` |

### Responsibilities

A slot holds a list of atomic responsibilities, each with a weight in `(0, 1]`
that scales its pull on the player's decisions; 1 is a primary duty. A slot may
hold none; its player keeps his place in the shape.

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

A slot must not hold duties that pull its player to different places:

- `provideWidth` and `occupyHalfspace`,
- `runInBehind` and `holdRestDefence`, `runInBehind` and `holdDefensiveLine`,
- `guardGoal` and any other responsibility.

At most one slot guards the goal.

#### Role presets

Familiar roles are presets (`rolePreset.hpp`) that fill a slot's
responsibilities. The tactic keeps only the responsibilities: the match never
knows a slot's preset, and a new role needs no code.

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
`transitionRisk`) weigh the components of the desired-region cost
(implementation plan, section 7.2) against each other.

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

`lineHeight + blockLength` must not exceed 1; such a block would reach past the
opponent's goal line.

### Validation

| Code                             | Cause                                                  |
|----------------------------------|--------------------------------------------------------|
| `kEmptyName`                     | the tactic has no name                                 |
| `kWrongSlotCount`                | neither seven nor eleven slots                          |
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
| `kInvalidSetPiece`               | a [set piece plan](set-pieces.md#plans) names a slot that does not exist or twice in a list, gives a kickoff runners or markers, or a delivery its kind cannot have |

Errors come in a fixed order: name, slots by index, principles, phases in the
order of the table above, then the set piece plans by kind.

### Formation presets

`formationSlots(formation)` (`formation.hpp`) fills the eleven slots of a
familiar formation, as editing help like the role presets: each slot a
position in the base shape and a role preset's responsibilities, in line-up
order — the goalkeeper, the back four from the touchline at `y = 0` to the
other, then midfield and attack the same way. Nothing in the match knows which
formation a tactic came from; it plays the atomic responsibilities.

| Formation | Midfield and attack                                                   |
|-----------|-----------------------------------------------------------------------|
| `4-4-2`   | two wingers, a holding and a central midfielder, two strikers          |
| `4-3-3`   | a holding and two central midfielders, two wingers and a striker       |

## Tactic file format

Tactic files under `data/tactics/` describe tactics, so a tactical identity can
be tuned without recompiling. `sim-tactics` reads and writes them
(`tacticJson.hpp`); the [tactic model](#tactic-model) defines what they hold.

### Version 4

```json
{
  "format": "elyverse-tactic",
  "version": 4,
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

The example is shortened: a file has seven or eleven slots and an entry for
each of the seven phases.

| Field              | Type   | Meaning                                                     |
|--------------------|--------|-------------------------------------------------------------|
| `format`           | string | always `"elyverse-tactic"`                                   |
| `version`          | integer| the format version, 4                                        |
| `name`             | string | the tactic's name; a file is named after it (`<name>.json`)  |
| `description`      | string | optional; what the tactic is for and which parameters express it |
| `slots`            | array  | seven or eleven slots, in line-up order                      |
| `slots[].position` | object | `depth` and `width`, fractions of the pitch                  |
| `slots[].role`     | string | a role preset, expanded into its responsibilities on load    |
| `slots[].responsibilities` | array | `{ "responsibility", "weight" }` entries          |
| `principles`       | object | `pressingLine`, `pressingTriggers`, `positioning` weights, `goalkeeper.sweeping` and `goalkeeper.directness` |
| `phases`           | object | one instruction per phase, keyed by phase name               |
| `setPieces`        | object | optional; a plan per set piece, `kickoff`, `throwIn`, `corner`, `goalKick`: `taker` (a slot or `null`), `delivery`, `runners` and `markers` (slots) — see [set pieces](set-pieces.md#plans); without it, the default plans |

Every field except `description` and `setPieces` is required. A slot has `role` or
`responsibilities`, not both: a preset plus extra duties would hide what the slot
holds. Names, values and ranges are the [model's](#tactic-model): phases
(`buildUp`, `progression`, ...), responsibilities (`provideWidth`, ...), role
presets (`winger`, ...) and pressing triggers (`backPass`, ...).

### Loading

`loadTactic(path)` and `parseTacticJson(text, source)` produce the same
validated `Tactic` as `Tactic::create()` and apply all its rules. They reject:

| Code                  | Cause                                                          |
|-----------------------|----------------------------------------------------------------|
| `kIoError`            | the file cannot be read                                        |
| `kMalformed`          | not JSON, another `format`, a missing, mistyped or unknown field, an unknown name |
| `kUnsupportedVersion` | a `version` other than 4                                        |
| `kInvalidTactic`      | the tactic breaks a rule of the model; every broken rule is listed |

Every message names the file and the field:

```text
data/tactics/pressing.json: phases.pressing.lineHeight: 1.3 must lie in [0, 1]
```

Unknown fields are errors, so a misspelled key cannot silently do nothing.

Loading is deterministic: the same file always gives an equal tactic.
`contentHash()` (`tacticHash.hpp`) hashes the loaded model, not the text, so
whitespace, key order, or a role written out as its responsibilities do not
change it; replays record it to name the tactic a team played. `toTacticJson()`
writes a tactic back, with responsibilities written out; reading that text gives
an equal tactic.

### Versioning

`version` goes up by one when a reader of the old format would misread a new
file: a field removed, renamed, or changed in type or meaning. Readers accept
only the version they know and reject every other one with an error naming it.
A new optional field with a documented default needs no new version; a new
required one does.

| Version | Change          |
|---------|-----------------|
| 1       | Initial format. |
| 2       | Adds the required `principles.goalkeeper.sweeping` ([goalkeeper](goalkeeper.md#positioning-and-sweeping)). |
| 3       | Adds the required `principles.goalkeeper.directness` ([goalkeeper distribution](goalkeeper.md#distribution)). |
| 4       | `slots` holds seven or eleven slots: eleven-a-side. A reader of version 3 would reject eleven. |

### Files

| File                          | Tactic                                               |
|-------------------------------|------------------------------------------------------|
| `data/tactics/reference.json` | the neutral reference tactic of `referenceTacticSpec()` |
| `data/tactics/reference-eleven.json` | the eleven-a-side reference of `referenceElevenASideTacticSpec()`: the 4-4-2 preset with the reference's principles and phases |
| `data/tactics/possession.json` | keeps the ball, counterpresses ([tactical identities](#tactical-identities)) |
| `data/tactics/counter.json`   | deep block, vertical after a regain                  |
| `data/tactics/pressing.json`  | high line, presses on every trigger                  |
| `data/tactics/possession-seven.json`, `counter-seven.json`, `pressing-seven.json` | the seven-a-side presets of the three identities: the same principles and phases on seven slots, for seven-a-side scenarios |

`tests/unit/sim-tactics/tacticFilesTests.cpp` loads every file in
`data/tactics/`, checks that each is named after its tactic, and that
`reference.json` equals the reference tactic built in code.

## Tactical identities

Three tactics under `data/tactics/` give the match recognisable styles:
`possession`, `counter` and `pressing`, eleven a side. They use the same
[model](#tactic-model) and [file format](#tactic-file-format) as
`reference-eleven.json` and differ **only in data**: no system knows their
names, and a new style is a new file. Their seven-a-side presets,
`possession-seven`, `counter-seven` and `pressing-seven`, keep the same
principles and phases on seven slots for the short scenarios that are seven a
side.

### Intent and parameters

| | possession | counter | pressing |
|---|---|---|---|
| Intent | keep the ball, circulate safely, counterpress hard on a sloppy loss | defend deep and compact, strike quickly and vertically after a regain | hunt the ball high, with several players, on every trigger |
| Shape | wide 4-3-3, wingers on the touchlines (seven a side 1-2-1-3) | low, narrow 4-4-1-1 with two holding midfielders and inside forwards (1-2-1-2-1) | high 4-3-3, short block (1-2-1-3 with a central midfielder) |
| `pressingLine` | 0.55 | 0.85 (rarely presses) | 0.4 (presses early) |
| Pressing triggers | poor first touch, back pass | none | all five |
| `lineHeight` out of possession | 0.35–0.4 | 0.15 | 0.4–0.45 |
| `pressingIntensity` | 0.4, 0.6 when pressing, 1.0 in defensive transition (counterpress) | 0.1–0.25 | 1.0 |
| `passingRisk` | 0.2–0.35 | 0.6–0.8, 0.9 in attacking transition | 0.5–0.75 |
| `runFrequency` | 0.2–0.35 | 0.5–0.9 | 0.3–0.7 |
| `blockWidth` in possession | 0.9–0.95 | 0.8 | 0.85 |

The positioning weights follow the same intent: possession values spacing and
occupancy and guards against transitions (`transitionRisk` 0.8); counter cares
least about occupancy and transition risk and most about reaching its targets.

`passingRisk` reaches the player on the ball through `scoringForRisk()`
([pass decisions](on-ball.md#pass-decisions)): the higher it is, the more a
pass gains for its ground and the less its interception risk counts.

### Playing them against each other

Any two tactics of the same size, seven or eleven, meet in the tactic match
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

- **Pressing wins the ball back higher.** Over sixty one-minute eleven-a-side
  tactic matches against the eleven-a-side reference tactic, pressing regains
  the ball more often than counter, and at least one and a half times as often
  in the opponent's half (at the time of writing 361 regains, 265 of them high,
  against 146 and 106).
- **Counter plays forward after a regain.** A hand-placed seven-a-side moment,
  with the seven-a-side presets: home's holding
  midfielder has just won the ball in midfield, the striker ahead of him with a
  defender near the lane, the centre backs behind him free. In its attacking
  transition, counter's first action — a pass, a carry or a dribble
  ([ball carrying](on-ball.md#ball-carrying)) — goes more than five metres
  forward in at least eight more of twenty seeds than possession's (at the time
  of writing 14 against 4).

`tacticFilesTests.cpp` also pins the direction in which the files differ, such
as pressing's lower pressing line and counter's deeper block, for the
eleven-a-side identities and their seven-a-side presets alike, so an edit
cannot quietly flatten an identity.

### Over whole matches

The table below was measured before matches ran to full time
([match clock](match-clock.md)): 90 minutes of ticks without halves, so no
goals or points; a rerun will add them.

The [style benchmark](tooling.md#a-round-robin-of-styles) plays every pairing
of the three identities. The full run — twenty 90-minute eleven-a-side matches
per pairing on the standard pitch, seed 1, restarts on, core 0.34.0, 180
matches — gives per style, over its 120 matches home and away, mean and 95 %
interval:

| Metric                  | possession            | counter              | pressing               |
|-------------------------|-----------------------|----------------------|------------------------|
| possession share        | 0.53 [0.53, 0.54]     | 0.45 [0.44, 0.45]    | 0.52 [0.51, 0.53]      |
| passes                  | 1095 [1068, 1122]     | **870** [842, 898]   | 1133 [1112, 1155]      |
| pass completion         | 0.70 [0.69, 0.71]     | 0.71 [0.69, 0.72]    | **0.67** [0.66, 0.68]  |
| mean pass length (m)    | 16.8 [16.7, 16.9]     | 16.9 [16.8, 17.0]    | **15.3** [15.2, 15.5]  |
| progressive passes      | 212 [203, 220]        | **135** [130, 140]   | 217 [211, 223]         |
| regains                 | 395 [382, 408]        | **306** [295, 317]   | **453** [443, 463]     |
| regains, attacking third| 78 [76, 81]           | **50** [46, 53]      | **108** [104, 111]     |
| presses                 | 473 [463, 482]        | **0**                | **909** [896, 923]     |
| PPDA                    | 2.94 [2.88, 3.00]     | **5.76** [5.53, 5.98]| **2.46** [2.43, 2.49]  |
| pitch control share     | 0.53 [0.53, 0.54]     | **0.44** [0.43, 0.45]| 0.52 [0.52, 0.53]      |
| ball in attacking third | **0.20** [0.19, 0.20] | 0.24 [0.22, 0.26]    | 0.24 [0.23, 0.24]      |
| shots                   | 45 [42, 47]           | 38 [36, 40]          | **71** [68, 74]        |

Pressing and counter separate — non-overlapping intervals — on 18 metrics,
possession and counter on 16, possession and pressing on 12:

- **Pressing** presses about twice as often as possession, wins the ball back
  most often and in the attacking third most of all, allows the fewest passes
  per defensive action and shoots the most. It pays with the lowest pass
  completion and the shortest passes.
- **Counter** never presses, allows by far the most passes per defensive action,
  controls the least of the pitch and plays the fewest passes; still, the ball
  spends as much time in its attacking third as in pressing's.
- **Possession** has about as much of the ball as pressing, slightly more on
  average, and the ball spends the least time in its attacking third: it
  circulates deep.

**What moved with eleven a side**, against the seven-a-side run at core 0.18.0:
pressing no longer clearly has the most of the ball — on the full pitch a press
covers less of it, so pressing regains about a fifth less and its share falls
back to possession's. Counter's PPDA rises from about 4 to almost 6, its
deep block giving the opponent more room; passes get longer everywhere, as the
pitch does. The seven-a-side presets play as before.

### What this is not

The styles are as distinct as the match systems let them be. Carriers pass to
where a teammate stands, not into his run, so a counter-attack is mostly a
sequence of forward passes.
