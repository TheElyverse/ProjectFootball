# Defensive shape

A side with a [tactic](tactics.md) that does not have the ball defends as a
block. Two layers make the block:

1. **The shape.** Every defender's [desired region](desired-region.md) comes from
   the phase instruction of the defending phase (`defensiveBlock`, `pressing`,
   `defensiveTransition`): the defensive line stands `lineHeight` up the pitch,
   the block spans `blockLength` and `blockWidth` -- its compactness -- and it
   shifts toward the ball by `ballShift`. A high line and a deep line differ only
   in data.
2. **Assignments.** Each defender decides what to do within the block, with the
   pipeline every decision uses: candidates from his own memory, weighted
   utilities, the seeded softmax, and the chosen action's target as his movement
   target (`defensiveActions.hpp`).

## Defensive actions

| Action          | Target                                                             | Offered when |
|-----------------|--------------------------------------------------------------------|--------------|
| `holdPosition`  | the centre of his desired region: his place in the block             | always |
| `markOpponent`  | `markDistance` (1.5 m) goal-side of the opponent, between him and the centre of the own goal | an opponent he remembers stands within `markRadius` (12 m) of his region: the nearest one |
| `trackRunner`   | goal-side of where the runner will be `trackLeadSeconds` (0.5 s) later | an opponent he remembers runs at the own goal at `runnerSpeed` (3 m/s) or more within `trackRadius` (15 m) of him: the fastest one |
| `cover`         | `coverDistance` (6 m) behind a teammate who presses the carrier, otherwise behind the teammate nearest the ball, toward the own goal | he remembers a teammate |
| `pressCarrier`  | a meter from the carrier on his line to his nearest option; see [pressing](pressing.md) | he remembers the carrier within `pressRadius` (15 m) |
| `blockLane`     | on the lane from the carrier to the option nearest him; see [pressing](pressing.md) | that point lies within `pressRadius` |

Opponents come from the defender's [memory](perception.md): **a run he has not
seen is not tracked**, and he marks where he believes his man is. The opponent's
goalkeeper -- an opponent within 8 m of his own goal line -- is nobody's man.
The own goalkeeper holds his region and decides nothing.

## Utility

The weighted parts are those of [off-ball movement](off-ball-movement.md), with
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

## When defenders decide

Defensive decisions follow the cadence of off-ball decisions: every 6 ticks within
20 m of the ball, every 18 ticks farther away. A decided action records whether
the player's team had the ball; when possession changes, every player decides
again at once, so nobody keeps attacking after his team has lost the ball.

## Diagnostics

Every decision is an `ActionDiagnostic`, with all candidates and their weighted
parts (see [match events](match-events.md)). The debug frames also carry, per
tick, each team's phase and shape -- defensive, midfield and front line, length,
width and centroid, as `measureTeamShape()` ([zones](zones.md)) measures them --
and each player's desired region and decided action: his defensive assignment.

## Scenarios

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
