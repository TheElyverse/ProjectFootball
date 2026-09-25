# Desired region

A player of a side with a [tactic](tactics.md) does not run to a fixed point.
He weighs a small landscape of positions around where his tactic wants him and
settles on the cheapest (implementation plan section 7.2):

```text
PositionCost = TacticalTargetDistance + SpacingPenalty + PressureCost
             + OccupancyPenalty + TransitionRisk
```

The result is his **desired region**, and its centre his movement target: the
same `target` the [movement model](player-movement.md) has always run to. There is
no separate movement path. `desiredRegion.hpp` holds the pieces.

## The tactical target

`tacticalTarget(state, playerIndex, phase)` is where the tactic wants the player,
from his slot in the base shape, his side's current [phase](match-phases.md), the
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
     [rest-defence](zones.md) depth band, 5 to 20 m behind the ball.
4. **The goalkeeper** (`guardGoal`) stands at his slot's base depth and follows
   the ball a quarter of the way across, within 15 % of the width of the
   centre.

For example, in progression with the ball on the centre spot of the 60 × 40 m
pitch, the reference tactic's block is 27 m long (0.45) and 34 m wide (0.85);
its nominal centre at 34.5 m moves a fifth of the way toward the ball at 30 m,
to 33.6 m, so the line stands at 20.1 m. A centre back stands on it, the
low-side winger 18 m further up and on the wing's centre line, 4 m from the
touchline.

## The cost of a position

`evaluatePosition()` gives every component separately, in a `PositionCost`:

| Component        | Meaning                                                              |
|------------------|----------------------------------------------------------------------|
| `targetDistance` | distance from the tactical target, per `targetDistanceScale` (10 m)   |
| `spacing`        | for each teammate he remembers within `spacingRadius` (6 m): (1 − d / 6 m)² |
| `pressure`       | for each opponent he remembers within `pressureRadius` (8 m): confidence × (1 − d / 8 m) |
| `occupancy`      | the opponent's [pitch control](pitch-control.md) of the position, 0.5 without a grid |
| `transitionRisk` | with the ball: how far the position is ahead of 5 m behind the ball, per third of the pitch, capped at 1, scaled by the player's defensive duty (0.25 plus his `holdRestDefence` and `holdDefensiveLine` weights, at most 1); 0 without the ball |
| `total`          | the sum, each component weighted by the tactic's `positioning` weights |

Teammates and opponents come from the player's own [memory](perception.md)
(confidence at least `minConfidence`), at the positions he believes they have
now; the ball and pitch control are read directly, as the ball pursuit does.

## Choosing the region

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

## The system

The tactical movement system runs every `intervalTicks` (6 ticks, 5 Hz). For every
side with a tactic and a phase, each player gets a new desired region and its
centre as his movement target, except:

- the player on the ball, and
- the side's chaser, whose target belongs to the [ball pursuit](reception.md).

While his team has the ball, a player may run to the target of an
[off-ball action](off-ball-movement.md) instead of his region's centre. A
scripted side is left alone. The system runs fourth in the
[standard order](match-loop.md), before pursuit, so a player pursuit sends after
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
