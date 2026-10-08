# Pass decisions

A player on the ball decides, several times a second, whether and where to pass
or to shoot. The decision reads only his own [perception](perception.md), picks
among his [pass candidates](pass-candidates.md) and his best
[shot](shot-decisions.md) with a seeded softmax, and leaves the kick to
[pass execution](passing.md). It lives in `sim-match` (`passDecision.hpp`)
and runs as the system `makePassDecisionSystem()`.

## The pipeline

```text
perception → candidates and utilities → softmax choice → PassIntent  → execution
 (memory)     generatePassCandidates()     chooseOnBall()   ShotIntent    ball system
              generateShotCandidates()                      pendingAction
```

Every `intervalTicks` ticks (6: five times a second at 30 Hz):

1. **Is there a decision to make?** Not while the ball is free or its owner
   already has an action pending.
2. **Hold.** A player keeps a ball he took less than `minHoldSeconds` (0.5 s) ago,
   measured from his last touch — a goalkeeper one he holds in his hands less
   than the distribution's `holdSeconds` (2 s). A player who owns the ball from
   the start of a state may pass at once.
3. **Options.** `generatePassCandidates()` and `generateShotCandidates()` list
   and score his options from his memory. A goalkeeper adds his long balls,
   and from his hands throws no farther than `throwRange`
   ([goalkeeper distribution](goalkeeper-distribution.md)).
4. **Choice.** `chooseOnBall()` picks one of the valid passes or the best valid
   shot. Without a valid option he keeps the ball.
5. **Intent.** The choice becomes his pending action — for a pass the target,
   planned speed, receiver and whether it is lofted, for a shot the aimed point, height and speed. The
   ball system plays it in the next step.

## Seeded softmax

Among the valid options, `chooseOnBall()` picks option `i` with probability

```text
p_i = exp((u_i − u_max) / T) / Σ exp((u_j − u_max) / T)
```

for utilities `u` and the temperature `T` (0.15). The best option is the most
likely, but not certain: two options 0.1 apart in utility are chosen about 2 : 1.
A lower temperature makes the choice greedier, a higher one more varied.

The draw comes from the simulation's `kAi` random stream, one number per choice
and none when there is nothing to choose. The exponential is `stableExp()` from
`sim-core`, built from basic arithmetic, so the same seed makes the same choices
on every platform; `std::exp` may differ in the last bit between standard
libraries.

## Timing

A decision in the step of tick `t` becomes the pending action; the ball system
executes it in the step of `t + 1`, when the ball leaves the passer. Choosing and
playing are separate steps, and several things keep a pass from being played
twice:

- no decision while his action is pending,
- only the owner can decide, and he no longer owns the ball once it is kicked,
- the ball system clears every pending pass and shot whether it plays it or not,
- the passer cannot take the ball back for the reclaim delay (see
  [reception](reception.md)).

## Configuration

`DecisionConfig` is part of `MatchConfig` and of every replay, and holds the
`PassScoringConfig` of [pass candidates](pass-candidates.md) and the
`ShotScoringConfig` of [shot decisions](shot-decisions.md):

| Field            | Default | Meaning                                         |
|------------------|---------|-------------------------------------------------|
| `intervalTicks`  | 6       | ticks between decisions                         |
| `minHoldSeconds` | 0.5 s   | a new owner keeps the ball at least this long   |
| `temperature`    | 0.15    | softmax temperature over utility                |
| `scoring`        |         | the pass candidates' scoring configuration      |
| `shooting`       |         | the shot candidates' scoring configuration      |
| `distribution`   |         | how a goalkeeper distributes the ball ([goalkeeper distribution](goalkeeper-distribution.md)) |

The system rejects an interval below one tick, a negative or non-finite hold, a
temperature that is not positive and finite, and invalid scoring or
distribution.

## Passing risk

A team with a tactic scales the scoring by the `passingRisk` `r` of its current
phase instruction ([tactics](tactics.md)) before the carrier decides;
`scoringForRisk()` does the arithmetic:

| Field               | Scaled by               | Effect of a bold tactic (`r` near 1)  |
|---------------------|-------------------------|---------------------------------------|
| `progressionWeight` | `0.5 + r`               | values ground gained more             |
| `riskWeight`        | `1.5 - r`               | fears interception less               |
| `minCompletion`     | `1.3 - 0.6 r`, at most 1 | offers less likely passes too        |

`r = 0.5`, the reference tactic's value, leaves the scoring unchanged, so a
team without a tactic and the reference tactic decide alike. A goalkeeper's
scoring also carries his tactic's directness as the `loftedBias` of his long
balls ([goalkeeper distribution](goalkeeper-distribution.md#the-tactics-dial)). The factors are
deliberately simple and linear: they make the styles distinguishable without a
second tuning surface next to the scoring itself.

## What this is not

A player on the ball only passes, shoots or keeps the ball: no dribble, no
carry, no turning to look for options; only a goalkeeper kicks long. Off-ball players do not decide yet; they hold their
position or chase a free ball (see [reception](reception.md)).
