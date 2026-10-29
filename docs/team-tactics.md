# Team tactics in the match

How a team's tactic becomes movement on the pitch: the phase it is in, who
controls which space, the zones and shape it holds, where each player wants to
be, how players move off the ball, and how the team defends and presses.

- [Match phases](#match-phases)
- [Pitch control](#pitch-control)
- [Zones and team shape](#zones-and-team-shape)
- [Desired region](#desired-region)
- [Off-ball movement](#off-ball-movement)
- [Defensive shape](#defensive-shape)
- [Pressing](#pressing)

## Match phases

A [tactic](tactics.md#tactic-model) gives instructions per tactical phase. The
tactical phase system (`tacticalPhases.hpp`) decides each team's phase from
possession, the ball's position and recent possession changes.

### Team possession

`MatchState::possession()` records which team has the ball:

- the team of the ball's owner, or, while the ball is free, the team of its last
  touch -- a pass in flight still belongs to the passer's team, and a loose ball
  to whoever played it last;
- `since`, the tick the team won the ball: the tick of the ball's last touch when
  the change is noticed;
- `fromOpponent`, whether it took the ball from the other team rather than from
  nobody, as at kickoff.

Possession is tracked for every match, with or without tactics.

### Phases

A side with a tactic is in one of the seven phases of [tactics](tactics.md#tactic-model):

| Situation                         | Phase                                                          |
|-----------------------------------|----------------------------------------------------------------|
| won the ball from the opponent less than `transitionSeconds` ago | `attackingTransition`           |
| lost the ball to the opponent less than `transitionSeconds` ago  | `defensiveTransition`           |
| has the ball, ball in the own third      | `buildUp`                                               |
| has the ball, ball in the middle third   | `progression`                                           |
| has the ball, ball in the opponent's third | `finalThird`                                          |
| without the ball, ball at least `pressingLine` up the pitch | `pressing`                           |
| without the ball, ball below the pressing line | `defensiveBlock`                                  |

Distances up the pitch are measured from the team's own goal line
(`depthOf()`, `teamFrame.hpp`); `pressingLine` is the tactic's principle, a
fraction of the pitch length. While nobody has had the ball the rules without
the ball apply, with no transition. A scripted side, one without a tactic, has
no phase.

**Hysteresis.** A team changes third, or switches between pressing and block,
only once the ball is `hysteresisMeters` past the boundary, so a ball rolling
along a boundary does not flicker the phase. A ball that jumps two thirds at
once, and the end of a transition, switch at once.

### The system

| Parameter           | Default | Meaning                                              |
|---------------------|---------|------------------------------------------------------|
| `intervalTicks`     | 10      | runs every 10 ticks, 3 Hz at 30 Hz: the tactical evaluation rate of implementation plan section 6.3 |
| `transitionSeconds` | 4.0     | how long a transition lasts after a possession change |
| `hysteresisMeters`  | 3.0     | margin past a boundary before the phase switches      |

`PhaseConfig` is part of `MatchConfig` and of every replay. The system runs
second in the [standard order](match-engine.md#match-loop), after perception; it writes team
possession and the phases, nothing else. When a side's phase changes it records
a `PhaseChanged` [event](match-engine.md#match-events-and-decision-diagnostics) with the side, the previous phase
(empty for the first) and the new one.

`classifyPhase()` (the rule above, from a `PhaseSituation`) and
`updatePossession()` are pure functions, testable without a match.

### Example

`tests/unit/sim-match/tacticalPhasesTests.cpp` plays the kickoff fixture with the
reference tactic for both sides. Home's forward gets the ball on the centre spot
at tick 0: home is in `progression`, away in `defensiveBlock`. At tick 60 away's
goalkeeper gets the ball: home enters `defensiveTransition` and away
`attackingTransition`. Four seconds later, at tick 180, away is in `buildUp` and
home, with the ball deep in away's half beyond its pressing line, in `pressing`.

## Pitch control

Pitch control estimates which team controls which part of the pitch, and how
clearly: a team owns the space its players reach first. It builds on
`estimateArrivalSeconds()` ([spatial queries](players.md#spatial-queries)) and
lives in `sim-match` (`pitchControlGrid.hpp`, `pitchControl.hpp`).

### The grid

`computePitchControl(state, config)` lays square cells of `cellSize` meters over
the pitch, from the corner `(0, 0)`: 30 × 20 cells of 2 m on the 60 × 40 m
sandbox pitch. A pitch that is not a whole number of cells gets one more, partial
column or row, whose centre may lie past the edge; its arrival times are taken at
the nearest point on the pitch.

For every cell and each side the grid stores the **earliest arrival time**: the
smallest `estimateArrivalSeconds()` of any of the side's players to the cell's
centre, from where he is and how he moves, by the
[movement model](players.md#player-movement). From the two arrival times follows the
**control** of a side over the cell, in `[0, 1]`:

```text
control(side) = 1 / (1 + e^((own arrival - other arrival) / controlSeconds))
```

Arriving together, both teams hold 1/2; a team `controlSeconds` (0.5 s)
earlier holds about 0.73, a second earlier about 0.88. The exponential is
`stableExp()`, so control is the same on every platform; both sides use the same
expression with the arrival times swapped, so mirrored positions give
bit-for-bit mirrored control.

Pitch control is a team-level service, not a player's belief: like ball pursuit
it uses true positions and velocities.

### Queries

| Query                         | Answer                                                     |
|-------------------------------|------------------------------------------------------------|
| `arrivalSeconds(side, cell)`  | the side's earliest arrival at the cell's centre            |
| `control(side, cell)`         | the side's control of the cell                              |
| `cellAt(position)`            | the cell a position lies in; off the grid, the nearest edge cell |
| `controlAt(side, position)`   | the control of that cell; 0.5 for a non-finite position     |
| `regionControl(side, rect)`   | the mean control of the cells whose centres lie in a `PitchRect`; empty if none does |
| `share(side)`                 | the mean control over the whole grid: the side's share of the pitch |

### The system

The grid is a cache: the pitch-control system recomputes it every
`intervalTicks` and stores it in the match state, `MatchState::pitchControl()`,
where the systems that weigh space read it until the next refresh. A state
created from a spec has none. The state hash includes the grid.

| Parameter        | Default | Meaning                                                  |
|------------------|---------|----------------------------------------------------------|
| `intervalTicks`  | 10      | refreshes every 10 ticks, three times a second at 30 Hz   |
| `cellSize`       | 2.0 m   | the side of a cell; at least `kMinPitchControlCellSize` (0.5 m) |
| `controlSeconds` | 0.5 s   | how sharply control changes with the arrival advantage    |

`PitchControlConfig` is part of `MatchConfig` and of every replay. The system
runs third in the [standard order](match-engine.md#match-loop), after the tactical phase,
writes the grid and records a `PitchControlSampled` [event](match-engine.md#match-events-and-decision-diagnostics)
with home's share of the pitch and the ball's position, so
[analytics](tooling.md#match-analytics) can measure control and territory from events
alone.

### Cost

A refresh estimates one arrival time per player and cell: 14 × 600 = 8400
estimates for seven-a-side with 2 m cells. The micro-benchmark in
`tests/unit/sim-match/pitchControlTests.cpp` measures it:

```sh
./build/release/tests/unit/sim-match-tests "Pitch control refresh cost"
```

| Build   | Mean cost per refresh (seven-a-side, 2 m cells) |
|---------|--------------------------------------------------|
| Release | 86 µs                                            |
| Debug   | 700 µs                                           |

Measured on a two-core Linux container with GCC 13. At three refreshes a
second that is about 0.3 ms of Release time per simulated second. Halving the
cell size quadruples the cost.

## Zones and team shape

Tactics speak of wings, halfspaces, thirds, the space between and behind the
lines. `sim-match` provides these as **derived data** (`zones.hpp`): queries
over the pitch and the current state. No rule treats a zone boundary as a hard
limit; responsibilities like "provide width" are evaluated against zones, but
zones steer nothing themselves (implementation plan section 6.4).

### Static zones

Static zones depend on the pitch only, and on which side is looking: lanes are
named left to right as seen by a player attacking the opponent's goal, thirds
from the side's own goal line.

| Lanes (`Lane`)                           | Across the width, from the side's left touchline |
|------------------------------------------|--------------------------------------------------|
| `leftWing`                               | the first fifth                                  |
| `leftHalfspace`                          | the second fifth                                 |
| `centre`                                 | the middle fifth                                 |
| `rightHalfspace`                         | the fourth fifth                                 |
| `rightWing`                              | the last fifth                                   |

Home attacks `+x`, so its left touchline is `y = width`; away's is `y = 0`. On the
60 × 40 m sandbox pitch every lane is 8 m wide. A position on a boundary belongs
to the lane further right, a position off the pitch to the nearest lane.

| Thirds (`Third`) | By depth from the side's own goal line |
|------------------|----------------------------------------|
| `defensive`      | the first third of the length          |
| `middle`         | the second                             |
| `attacking`      | the last                               |

`laneOf()`, `thirdOf()` classify a position; `laneRect()`, `thirdRect()` return
the zone as a `PitchRect` (`pitch.hpp`), and `laneCenterY()` a lane's centre line.

### Team shape

`measureTeamShape(state, side)` measures how the side's outfield players stand
right now, as depths from its own goal line in meters:

| Field           | Meaning                                                      |
|-----------------|--------------------------------------------------------------|
| `defensiveLine` | the deepest outfield player: the last line                   |
| `midfieldLine`  | the median outfield depth                                    |
| `frontLine`     | the highest outfield player                                  |
| `length`        | front line minus defensive line: the vertical compactness     |
| `width`, `minY`, `maxY` | the outermost outfield players across the pitch: the horizontal compactness |
| `centroid`      | the mean outfield position, in pitch coordinates              |

The goalkeeper -- the player whose slot holds `guardGoal` in his side's tactic,
see `isGoalkeeper()` -- is not an outfield player. A scripted side has no
goalkeeper, so its shape includes everyone.

### Dynamic zones

Dynamic zones follow a team's current shape:

| Query                                  | Zone                                                          |
|----------------------------------------|---------------------------------------------------------------|
| `betweenLines(opponent, shape)`        | from the opponent's defensive line to his midfield line, across the width his outfield players span: room to receive between the lines |
| `behindLine(opponent, shape)`          | from his defensive line to his goal line, full width: where runs in behind go |
| `restDefenceZone(side, ballPosition)`  | 5 to 20 m behind the ball, toward the own goal and never past it, across the centre and both halfspaces: where a side in possession keeps players against a counterattack |

The rest-defence distances are `kRestDefenceNear` and `kRestDefenceFar`, the
lane fraction `kLaneFraction`: sandbox choices, not tuned per tactic.

## Desired region

A player of a side with a [tactic](tactics.md#tactic-model) does not run to a
fixed point. He weighs positions around where his tactic wants him and takes the
cheapest (implementation plan section 7.2):

```text
PositionCost = TacticalTargetDistance + SpacingPenalty + PressureCost
             + OccupancyPenalty + TransitionRisk
```

The result is his **desired region**; its centre is his movement target, the
ordinary `target` of the [movement model](players.md#player-movement).
`desiredRegion.hpp` holds the pieces.

### The tactical target

`tacticalTarget(state, playerIndex, phase)` is where the tactic wants the player,
from his slot in the base shape, his side's current [phase](#match-phases), the
ball and his responsibilities:

1. **The block.** The outfield slots' base positions span a depth range and a
   width range; the phase instruction scales them into a block whose defensive
   line stands `lineHeight` up the pitch, which spans `blockLength` of the
   length and `blockWidth` of the width. A slot at the deepest base depth
   stands on the line, one at the highest base depth at the block's front.
2. **The ball.** The block's centre moves toward the ball: across by
   `ballShift`, up and down by half of it. The line never goes past either goal
   line.
3. **Responsibilities** pull the target, by their weight, toward:
   - `provideWidth`: the wing's centre line on the slot's side of the pitch (a
     tenth of the width in from the touchline),
   - `occupyHalfspace`: the halfspace's centre line on that side (three
     tenths in),
   - `holdRestDefence`, while the team has the ball: the
     [rest-defence](#zones-and-team-shape) depth band, 5 to 20 m behind the ball.
4. **The goalkeeper** (`guardGoal`) has a target of his own,
   `goalkeeperTarget()`: on the bisector of his goal's angle as seen from the
   ball, as deep as the ball and his defensive line make it, and he runs
   straight to it without the costs below ([goalkeeper](goalkeeper.md#positioning-and-sweeping)).
   `tacticalTarget()` rejects him.

For example, in progression with the ball on the centre spot of the 60 × 40 m
pitch, the reference tactic's block is 27 m long (0.45) and 34 m wide (0.85);
its nominal centre at 34.5 m moves a fifth of the way toward the ball at 30 m,
to 33.6 m, so the line stands at 20.1 m. A centre back stands on it, the
low-side winger 18 m further up and on the wing's centre line, 4 m from the
touchline.

### The cost of a position

`evaluatePosition()` gives every component separately, in a `PositionCost`:

| Component        | Meaning                                                              |
|------------------|----------------------------------------------------------------------|
| `targetDistance` | distance from the tactical target, per `targetDistanceScale` (10 m)   |
| `spacing`        | for each teammate he remembers within `spacingRadius` (6 m): (1 − d / 6 m)² |
| `pressure`       | for each opponent he remembers within `pressureRadius` (8 m): confidence × (1 − d / 8 m) |
| `occupancy`      | the opponent's [pitch control](#pitch-control) of the position, 0.5 without a grid |
| `transitionRisk` | with the ball: how far the position is ahead of 5 m behind the ball, per third of the pitch, capped at 1, scaled by the player's defensive duty (0.25 plus his `holdRestDefence` and `holdDefensiveLine` weights, at most 1); 0 without the ball |
| `total`          | the sum, each component weighted by the tactic's `positioning` weights |

Teammates and opponents come from the player's own [memory](players.md#perception)
(confidence at least `minConfidence`), at the positions he believes they have
now; the ball and pitch control are read directly, as the ball pursuit does.

### Choosing the region

`chooseDesiredRegion()` evaluates a bounded set of candidates, at most 18:

- the tactical target,
- eight directions around it at `candidateSpacing` (3 m) and twice that,
  kept on the pitch,
- the player's current centre, if he has one.

The cheapest candidate wins, ties to the earlier one. Two rules keep players
from oscillating between similar positions:

- **Hysteresis.** The current centre keeps `hysteresisCost` (0.15) of advantage:
  a player moves only to a clearly better position.
- **Smoothing.** The centre moves at most `maxShiftMeters` (3 m) per evaluation,
  so a region glides rather than jumps when the situation changes.

The region records the tactical target, the centre and the centre's cost; it is
part of the player's tactical state, `MatchState::tactical(playerIndex)`, and of
the state hash.

### The system

The tactical movement system runs every `intervalTicks` (6 ticks, 5 Hz). For every
side with a tactic and a phase, each player gets a new desired region and its
centre as his movement target, except:

- the player on the ball, and
- the side's chaser, whose target belongs to the [ball pursuit](ball.md#reception-interceptions-and-loose-balls).

While his team has the ball, a player may run to the target of an
[off-ball action](#off-ball-movement) instead of his region's centre. A
scripted side is left alone. The system runs fourth in the
[standard order](match-engine.md#match-loop), before pursuit, so a player pursuit sends after
the ball in the same step keeps pursuit's target.

| Parameter             | Default | Meaning                                       |
|-----------------------|---------|-----------------------------------------------|
| `intervalTicks`       | 6       | ticks between evaluations                      |
| `candidateSpacing`    | 3 m     | distance between the candidate rings           |
| `targetDistanceScale` | 10 m    | one unit of target distance                    |
| `spacingRadius`       | 6 m     | teammates closer than this crowd a position     |
| `pressureRadius`      | 8 m     | opponents closer than this press on it          |
| `minConfidence`       | 0.3     | memories less sure than this are ignored        |
| `hysteresisCost`      | 0.15    | the current region's advantage                  |
| `maxShiftMeters`      | 3 m     | the most a centre moves per evaluation          |

`PositioningConfig` is part of `MatchConfig` and of every replay.

## Off-ball movement

When a side with a [tactic](tactics.md#tactic-model) has the ball, each of the
carrier's teammates decides what to do without it, with the pipeline the player
on the ball uses to [pass](on-ball.md#pass-decisions):

```text
perception → candidates and utilities → softmax choice → action → movement target
 (memory)     generateOffBallCandidates()  chooseByUtility()  PlayerAction  movement
```

The decisions live in `offBallActions.hpp`; the tactical movement system
(`tacticalMovement.hpp`) runs them together with the
[desired region](#desired-region).

### Candidates

A teammate of the carrier weighs six actions, in this fixed order:

| Action            | Target                                                              |
|-------------------|---------------------------------------------------------------------|
| `holdPosition`    | the centre of his desired region                                     |
| `supportCarrier`  | the spot with the most open passing lane from the carrier, counting the detour from his region: on a ring `supportDistance` (10 m) around the carrier, or a third or two thirds of that from his region's centre |
| `moveIntoSpace`   | the point his team controls most, up to `spaceSearchRadius` (12 m) away, forward directions first |
| `runInBehind`     | `runDepth` (6 m) past the opponent's defensive line as he remembers it -- the second deepest opponent, the deepest being the goalkeeper -- where he is across the pitch; only if he remembers two opponents and the run is at least 3 m |
| `createWidth`     | the wing's centre line on his slot's side of the pitch, where he is along it |
| `occupyHalfspace` | the halfspace's centre line on that side                             |

Everything comes from the player's own [memory](players.md#perception): the opponents
he remembers, and the carrier where he believes him -- the ball itself if he does
not remember the carrier, since everyone watches the ball. Pitch control is his
team's.

### Utility

Each candidate's utility is the sum of weighted parts, `ActionScores`, kept apart
so the decision can be explained:

| Part             | Weight (default)            | Value                                        |
|------------------|-----------------------------|----------------------------------------------|
| `responsibility` | `responsibilityWeight` (1.0) | the weight of the matching responsibility in his slot: `supportCarrier`, `runInBehind`, `provideWidth`, `occupyHalfspace`; `holdResponsibility` (0.4) for holding position; 0 for space |
| `region`         | `regionWeight` (0.5)         | minus how much the target costs more than his desired region, by the [positioning cost](#desired-region), capped at 2 |
| `space`          | `spaceWeight` (0.6)          | his team's pitch control at the target        |
| `lane`           | `laneWeight` (0.8)           | how open the passing lane from the carrier to the target is: the nearest remembered opponent's distance from it over `laneRadius` (4 m), at most 1 |
| `urgency`        | `urgencyWeight` (0.6)        | the phase's `runFrequency` for runs in behind and into space; how closely the carrier is pressed -- 1 − distance / `pressedRadius` (6 m) of the nearest opponent -- for support |
| `effort`         | `effortWeight` (0.3)         | minus the distance to the target over `effortScale` (20 m) |

`dominantScore()` names the part that contributed most, by absolute value.

### Choice and execution

`chooseByUtility()` is the seeded softmax of pass decisions, with temperature
`temperature` (0.2) and one draw from the `kAi` stream per decision. The chosen
action, its target, whom it is about and the tick are the player's
`PlayerAction`, part of his tactical state and of the state hash. He runs to its
target until he decides again; holding position follows his desired region as it
moves.

**Relevance.** A player decides more often where it matters: within
`nearBallRadius` (20 m) of the ball every `nearIntervalTicks` (6), farther away
every `farIntervalTicks` (18) -- five and five-thirds times a second at 30 Hz.
When his team loses the ball he decides again at once, then as a
[defender](#defensive-shape). The goalkeeper decides only with his team on
the ball, between holding his region and supporting the carrier from inside his
own penalty area (`generateKeeperSupportCandidates()`,
[goalkeeper](goalkeeper.md#positioning-and-sweeping)).

**Diagnostics.** Every decision is reported as an `ActionDiagnostic`: all
candidates with their weighted parts and utility, and the index of the chosen
one. They are collected only on request, like pass decisions, and appear in the
debug frames as `actions`, each candidate with its `dominant` part.

`OffBallConfig` holds every parameter above and is part of `MatchConfig` and of
every replay.

### Staying onside

With [offside](match-engine.md#offside) on, a teammate of the carrier keeps his
movement target onside: half a metre short of the offside line he believes —
`believedOffsideLine()` of the opponents he remembers and the ball; with fewer
than two remembered, the ball or the halfway line — whatever his action, so a
target beyond it moves straight back. The exception is a **run in behind
started from an onside position**, at least a metre short of the line: his run
may take him past it, since a pass played before he crosses it is onside. Once
he is within that metre, the run is held short of the line too, and he waits
there rather than hover across it. A high line therefore pushes the attackers back, and is beaten by a run
timed from onside. Standing in an offside position on the way back is passive,
and no offence.

### Scenarios

`tests/unit/sim-match/offBallActionsTests.cpp` checks the behavior the tactic
asks for:

- **Opening a lane.** Home's striker has the ball, pressed by an opponent four
  meters in front of him who stands right in the lane to the holding midfielder
  ten meters behind. The midfielder picks a target the presser does not screen
  -- at least 3 m from the lane -- in at least 18 of 20 seeds.
- **Keeping width.** Over ten seconds of possession the winger who provides
  width stays in the wing lane at least 90 % of the time; the same slot without
  the duty, in a narrow block, at most half of it.
- Candidates, their order and weighted parts; decision frequency by relevance;
  and actions ending with possession.

## Defensive shape

A side with a [tactic](tactics.md#tactic-model) that does not have the ball defends as a
block. Two layers make the block:

1. **The shape.** Every defender's [desired region](#desired-region) comes from
   the phase instruction of the defending phase (`defensiveBlock`, `pressing`,
   `defensiveTransition`): the defensive line stands `lineHeight` up the pitch,
   the block spans `blockLength` and `blockWidth` -- its compactness -- and it
   shifts toward the ball by `ballShift`. A high line and a deep line differ only
   in data.
2. **Assignments.** Each defender decides what to do within the block, with the
   pipeline every decision uses: candidates from his own memory, weighted
   utilities, the seeded softmax, and the chosen action's target as his movement
   target (`defensiveActions.hpp`).

### Defensive actions

| Action          | Target                                                             | Offered when |
|-----------------|--------------------------------------------------------------------|--------------|
| `holdPosition`  | the centre of his desired region: his place in the block             | always |
| `markOpponent`  | `markDistance` (1.5 m) goal-side of the opponent, between him and the centre of the own goal | an opponent he remembers stands within `markRadius` (12 m) of his region: the nearest one |
| `trackRunner`   | goal-side of where the runner will be `trackLeadSeconds` (0.5 s) later | an opponent he remembers runs at the own goal at `runnerSpeed` (3 m/s) or more within `trackRadius` (15 m) of him: the fastest one |
| `cover`         | `coverDistance` (6 m) behind a teammate who presses the carrier, otherwise behind the teammate nearest the ball, toward the own goal | he remembers a teammate |
| `pressCarrier`  | a meter from the carrier on his line to his nearest option; see [pressing](#pressing) | he remembers the carrier within `pressRadius` (15 m) |
| `blockLane`     | on the lane from the carrier to the option nearest him; see [pressing](#pressing) | that point lies within `pressRadius` |

Opponents come from the defender's [memory](players.md#perception): **a run he has not
seen is not tracked**, and he marks where he believes his man is. The opponent's
goalkeeper is nobody's man: the opponent whose slot guards the goal, wherever
he stands, or for a scripted side an opponent within 8 m of his own goal line
(`keepsGoal()`). The own goalkeeper decides nothing here: he keeps goal and
comes for balls by his own model ([goalkeeper](goalkeeper.md#positioning-and-sweeping)).

### Utility

The weighted parts are those of [off-ball movement](#off-ball-movement), with
defensive meanings:

| Part             | Weight (default)             | Value |
|------------------|------------------------------|-------|
| `responsibility` | `responsibilityWeight` (1.0) | holding: the larger of `holdResponsibility` (0.4) and his `holdDefensiveLine` weight; marking: `markOpponent`; tracking: the larger of `markOpponent` and `cover`; covering: `cover`; pressing and blocking: `closePressingLine` |
| `region`         | `regionWeight` (0.5)         | minus how much the target costs more than his desired region, capped at 2 |
| `space`          | `spaceWeight` (0.4)          | the opponent's pitch control at the target: the danger of leaving that space open |
| `urgency`        | `urgencyWeight` (0.8)        | marking: the man's threat -- half how close he is to the own goal, half how close to the ball (within 30 m); tracking: the runner's speed over the defender's top speed; covering: 1 behind a presser, otherwise how close the covered teammate is to the ball (within 15 m); pressing and blocking: the phase's `pressingIntensity` times how close he is, 1 − distance / `pressRadius` |
| `effort`         | `effortWeight` (0.3)         | minus the distance to the target over `effortScale` (20 m) |

The choice uses `temperature` (0.2). `DefensiveConfig` is part of `MatchConfig`
and of every replay.

### When defenders decide

Defensive decisions follow the cadence of off-ball decisions: every 6 ticks within
20 m of the ball, every 18 ticks farther away. A decided action records whether
the player's team had the ball; when possession changes, every player decides
again at once, so nobody keeps attacking after his team has lost the ball.

### Diagnostics

Every decision is an `ActionDiagnostic`, with all candidates and their weighted
parts (see [match events](match-engine.md#match-events-and-decision-diagnostics)). The debug frames also carry, per
tick, each team's phase and shape -- defensive, midfield and front line, length,
width and centroid, as `measureTeamShape()` ([zones](#zones-and-team-shape)) measures them --
and each player's desired region and decided action: his defensive assignment.

### Scenarios

`tests/unit/sim-match/defensiveShapeTests.cpp`, with home defending with the
reference tactic against a scripted away side:

- a forward between the centre backs is marked goal-side, 1.5 m from him, and
  covering is offered;
- with the ball on one wing and then the other, home's centroid shifts more than
  6 m toward it;
- a tactic whose defending phases hold the line at 0.4 of the pitch stands more
  than 8 m higher after five seconds than one at 0.12;
- a run in front of the defence is seen and offered for tracking; the same run
  starting behind every outfield defender's back is not.

## Pressing

Pressing has three parts: the defensive actions players of a side with a
[tactic](tactics.md#tactic-model) choose in their
[defensive decisions](#defensive-shape) (PF-012), the duel a presser takes the
carrier on in, and what pressure does to the carrier's pass.

### The actions

Each action's geometry is a pure function of positions (`pressingActions.hpp`),
so it can be tested on its own:

- **Pressing the carrier** (`pressCarrier`). `pressTarget()` aims
  `pressDistance` (1 m) from the carrier, on his line to his likeliest option —
  the nearest teammate the presser remembers — so the run closes that pass as
  it closes him down. Without an option he aims goal-side of the carrier.
  Offered within `pressRadius` (15 m) of a carrier he remembers.
- **Blocking a lane** (`blockLane`). `laneBlockTarget()` is the point of the lane
  from the carrier to the option nearest the player, at least `laneMinDistance`
  (2 m) from either end -- between them, not on top of one; the middle of a lane
  shorter than twice that. Standing there raises the carrier's own estimate of
  the interception risk of that pass: in the unit test by more than 0.3.
- **Covering the presser** (`cover`). `coverTarget()` is `coverDistance` (6 m)
  behind a teammate who presses, toward the own goal, to catch the carrier if he
  gets past. A defender reads a teammate's pressing intent from his decided
  action; without a presser, cover protects the teammate nearest the ball.

Pressing and blocking weigh the `closePressingLine` responsibility; the phase's
`pressingIntensity` and how close the player is urge them. Every candidate names
its action, target and subject in the diagnostics.

While the carrier's ball rolls ahead after a touch
([ball carrying](on-ball.md#ball-carrying)), a presser goes on at the carrier,
not the ball — unless he reaches it first; then pursuit sends him after it as
after a free ball ([reception](ball.md#going-after-the-ball)).

### A carrier who stands on the ball

Without pressing — a deep block, a low `pressingIntensity` — a carrier could
stand on the ball forever, even with a defender a step away. So the
[tactical movement](#desired-region) sends one player at him: once the carrier
has had the ball for more than `idleSeconds` (2 s) since his last touch, the
nearest opponent outfield player within `idlePressRadius` (3 m) — first in
player order on a tie — decides `pressCarrier`, aimed `pressDistance`
goal-side of him, whatever his phase. A role in a coordinated press comes
first; a goalkeeper with the ball in his hands is never pressed. Teammates
react through their ordinary decisions. The decision trace marks the action
`idlePress`. Both values are in `DefensiveConfig`
([defensive shape](#defensive-shape)).

### Winning the ball

A presser who gets close to a carrier with the ball at his feet takes him on in
a [duel](duels.md#who-duels), which the carrier keeps or the presser wins,
knocks loose or fouls. Only a player whose decided action is `pressCarrier` --
his own, an assigned role in a coordinated press, or the
[idle press](#a-carrier-who-stands-on-the-ball) -- takes a carrier on, so a
side wins the ball at the carrier's feet only by pressing him.

### Pressure spoils passes

A pressed carrier plays worse: [pass execution](on-ball.md#passing) grows both errors by
`1 + pressureErrorFactor × pressure`, where pressure is 1 − distance /
`pressureRadius` (3 m) for a lone opponent, and each further opponent within
that radius adds to it: pressure is one minus the product of
min(1, distance / `pressureRadius`) over them. At full pressure the errors
double. So a press also wins the ball through the pass it forces, more so with
teammates closing in.

### Coordinated presses

Individual actions win the ball only when teammates work together. The "team
pressing" system (`teamPressing.hpp`) runs five times a second, before the
tactical movement, for every side with a tactic (GDD section 8.7).

#### Triggers

A side reacts to the triggers its tactic selects (`pressingTriggers`). A trigger
counts once an outfield player within `triggerRadius` (20 m) of the ball spots
it in what he observes: he must have seen the carrier -- or, for a slow pass,
the ball -- within the last perception interval.

| Trigger                 | Spotted when                                                    |
|-------------------------|-----------------------------------------------------------------|
| `poorFirstTouch`        | the carrier received within `recentSeconds` (1.5 s) a ball arriving at `heavyTouchSpeed` (11 m/s) or more |
| `backPass`              | the carrier just received a teammate's pass, with no shot since, that ended at least `backPassMeters` (2 m) deeper than it started |
| `receiverFacingOwnGoal` | the carrier just received and faces his own goal: the cosine to his attacking direction below `facingOwnGoal` (−0.3) |
| `isolatedReceiver`      | the carrier just received and the defender remembers no teammate of his within `isolationRadius` (10 m) |
| `slowPass`              | a recent pass to a receiver of the other side rolls slower than `slowPassSpeed` (6 m/s): the receiver is pressed before it arrives |

The facts behind them come from the match: the ball system records the last pass
(passer, origin, tick, receiver) and the last reception (player, tick, the ball's
speed) in the state.

A side in the pressing phase whose pressing intensity is at least
`phaseIntensity` (0.5) also presses whoever has the ball, without a trigger.

#### Who joins, in which role

The phase's `pressingIntensity` decides how many players join: intensity times
`maxJoiners` (4), rounded. None joins at intensity below 0.125.
`assignPressRoles()` gives them roles that close the carrier's nearest options,
by where the players stand:

1. the outfield player nearest the carrier presses him;
2. from three joiners, the free player nearest the spot behind the presser
   covers him;
3. the rest block the lanes to the carrier's nearest options (his teammates but
   the goalkeeper, `keepsGoal()`), each option taken by the free player who stands nearest his
   blocking spot.

A side with fewer outfield players, or a carrier with fewer options, gets
fewer roles; ties go to the lower id.

The tactical movement gives each player his role's action instead of letting him
choose -- the presser cuts off the nearest option nobody blocks -- and the
diagnostic marks it assigned. Everyone else keeps deciding for himself.

#### How a press ends

| Outcome          | When                                                        |
|------------------|-------------------------------------------------------------|
| `ballRegained`   | the pressing side has the ball                               |
| `passedOut`      | another player of the carrier's side has it: played out of pressure |
| `carrierEscaped` | neither after `maxPressSeconds` (4 s)                        |

`PressingStarted` (side, carrier, trigger -- empty for a phase press -- and the
roles) and `PressingEnded` (side, outcome) are [events](match-engine.md#match-events-and-decision-diagnostics). The
press in progress is part of the state, `MatchState::press(side)`.
`PressingConfig` is part of `MatchConfig` and of every replay.

#### Coordination pays

`tests/acceptance/p2AcceptanceTests.cpp` plays a hundred one-minute matches
against the reference tactic, restarts off so a ball out stays on the line,
home pressing high on four triggers: once at
intensity 0.25 (a lone presser), once at 1.0 (four players). The coordinated
press wins the ball in a larger share of its presses, at least three points
more: the lone presser leaves lanes open, and close teammates spoil the passes
the carrier does play. At the time of writing the coordinated press regains the
ball in 0.38 of its presses, the lone presser in 0.32. Ten matches were too few:
the two shares differ by about three standard errors over a hundred, and a
change as small as players tiring over the match
([fitness](players.md#fitness)) turned ten seeds round.
