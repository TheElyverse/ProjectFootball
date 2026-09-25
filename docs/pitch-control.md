# Pitch control

Pitch control estimates which team can control which part of the pitch, and how
clearly: the space a team owns is where its players get to first. It builds on
the arrival-time estimate of [spatial queries](spatial-queries.md)
(`estimateArrivalSeconds()`) and lives in `sim-match` (`pitchControlGrid.hpp`,
`pitchControl.hpp`).

## The grid

`computePitchControl(state, config)` lays square cells of `cellSize` meters over
the pitch, from the corner `(0, 0)`: 30 × 20 cells of 2 m on the 60 × 40 m
sandbox pitch. A pitch that is not a whole number of cells gets one more, partial
column or row, whose centre may lie past the edge; its arrival times are taken at
the nearest point on the pitch.

For every cell and each side the grid stores the **earliest arrival time**: the
smallest `estimateArrivalSeconds()` of any of the side's players to the cell's
centre, from where he is and how he moves, by the
[movement model](player-movement.md). From the two arrival times follows the
**control** of a side over the cell, in `[0, 1]`:

```text
control(side) = 1 / (1 + e^((own arrival - other arrival) / controlSeconds))
```

Both teams arriving together share a cell at 1/2; a team `controlSeconds` (0.5 s)
earlier holds about 0.73 of it, a team a second earlier about 0.88. The
exponential is `stableExp()`, so control is the same on every platform, and the
two sides' controls are the same expression with the arrival times swapped:
mirrored positions give bit-for-bit mirrored control.

Pitch control is a team-level spatial service, not a player's belief: it uses the
true positions and velocities, like the ball pursuit does. Decisions that weigh
space use it as the team's sense of where there is room.

## Queries

| Query                         | Answer                                                     |
|-------------------------------|------------------------------------------------------------|
| `arrivalSeconds(side, cell)`  | the side's earliest arrival at the cell's centre            |
| `control(side, cell)`         | the side's control of the cell                              |
| `cellAt(position)`            | the cell a position lies in; off the grid, the nearest edge cell |
| `controlAt(side, position)`   | the control of that cell; 0.5 for a non-finite position     |
| `regionControl(side, rect)`   | the mean control of the cells whose centres lie in a `PitchRect`; empty if none does |
| `share(side)`                 | the mean control over the whole grid: the side's share of the pitch |

## The system

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
runs third in the [standard order](match-loop.md), after the tactical phase,
writes the grid and records a `PitchControlSampled` [event](match-events.md)
with home's share of the pitch and the ball's position, so
[analytics](match-analytics.md) can measure control and territory from events
alone.

## Cost

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

Measured on the two-core Linux container the project's cloud sessions use, GCC
13. At three refreshes a second that is about 0.3 ms of Release time per
simulated second. The cost grows with the number of cells: halving the cell size
quadruples it.
