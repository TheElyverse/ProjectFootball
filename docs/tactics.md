# Tactics

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
│   └─ positioning { targetDistance, spacing, pressure, occupancy, transitionRisk }
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
`data/tactics/` describe them in the [tactic file format](tactic-format.md).

## Units and conventions

Everything is a fraction of the pitch, so one tactic fits any pitch size.

- **Depth** runs from the team's own goal line (0) to the opponent's (1). A
  shape reads the same for both teams, whichever way they attack.
- **Width** runs from the touchline at `y = 0` (0) to the one at `y = width` (1)
  in [pitch coordinates](match-geometry.md), for both teams alike: a slot at
  width 0.1 plays near the same touchline for home and away.
- **Heights and lengths** in phase instructions are fractions of the pitch
  length, **widths** fractions of the pitch width.
- **Dials** (intensity, risk, frequency, shift) are dimensionless, in `[0, 1]`.

A tactic has exactly seven slots: P2 is seven-a-side, eleven-a-side arrives with
the P3 baseline. The player in slot `i` is the `i`-th player of his side in the
match, in the order the match state lists them (his place in the line-up).

## Base shape

Each slot has a `position` in the base shape: where the player stands relative
to his teammates. The base shape gives the arrangement; the phase instructions
give its height and compactness in the match.

| Field   | Range    | Meaning                                         |
|---------|----------|-------------------------------------------------|
| `depth` | `[0, 1]` | from the own goal line (0) to the opponent's (1) |
| `width` | `[0, 1]` | from the touchline at `y = 0` to the one at `y = width` |

## Responsibilities

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

### Role presets

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

## Principles

| Field              | Range      | Meaning                                                  |
|--------------------|------------|----------------------------------------------------------|
| `pressingLine`     | `[0, 1]`   | how far up the pitch, from the own goal line, the ball must be before a team without it is in the pressing phase |
| `pressingTriggers` | list       | the situations that start a press, each at most once      |
| `positioning.*`    | `[0, 10]`  | weights of the positioning cost components               |

Pressing triggers (GDD section 8.7): `poorFirstTouch`, `backPass`,
`receiverFacingOwnGoal`, `isolatedReceiver`, `slowPass`.

The positioning weights (`targetDistance`, `spacing`, `pressure`, `occupancy`,
`transitionRisk`) weigh the components of the desired-region cost of the
implementation plan, section 7.2, against each other.

## Phases

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
| `passingRisk`       | `[0, 1]`  | how much risk the player on the ball accepts going forward  |
| `runFrequency`      | `[0, 1]`  | how often players run into space instead of holding position |

`lineHeight + blockLength` must not exceed 1: such a block would reach past the
opponent's goal line.

## Validation

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
