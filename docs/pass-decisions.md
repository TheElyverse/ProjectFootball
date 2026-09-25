# Pass decisions

A player on the ball decides, several times a second, whether and where to pass.
The decision reads only his own [perception](perception.md), picks among his
[pass candidates](pass-candidates.md) with a seeded softmax, and leaves the kick
to [pass execution](passing.md). It lives in `sim-match` (`passDecision.hpp`)
and runs as the system `makePassDecisionSystem()`.

## The pipeline

```text
perception → candidates and utilities → softmax choice → PassIntent → execution
 (memory)     generatePassCandidates()     choosePass()     pendingPass   ball system
```

Every `intervalTicks` ticks (6: five times a second at 30 Hz):

1. **Is there a decision to make?** Not while the ball is free or a pass is
   already pending.
2. **Hold.** A player keeps a ball he took less than `minHoldSeconds` (0.5 s) ago,
   measured from his last touch. A player who owns the ball from the start of a
   state may pass at once.
3. **Options.** `generatePassCandidates()` lists and scores his options from his
   memory.
4. **Choice.** `choosePass()` picks one of the valid candidates. Without a valid
   candidate he keeps the ball.
5. **Intent.** The choice becomes the pending pass — target, planned speed,
   receiver. The ball system plays it in the next step.

## Seeded softmax

Among the valid candidates, `choosePass()` picks candidate `i` with probability

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

A decision in the step of tick `t` becomes the pending pass; the ball system
executes it in the step of `t + 1`, when the ball leaves the passer. Choosing and
playing are separate steps, and several things keep a pass from being played
twice:

- no decision while a pass is pending,
- only the owner can decide, and he no longer owns the ball once it is kicked,
- the ball system clears the pending pass whether it plays it or not,
- the passer cannot take the ball back for the reclaim delay (see
  [reception](reception.md)).

## Configuration

`DecisionConfig` is part of `MatchConfig` and of every replay, and holds the
`PassScoringConfig` of [pass candidates](pass-candidates.md):

| Field            | Default | Meaning                                         |
|------------------|---------|-------------------------------------------------|
| `intervalTicks`  | 6       | ticks between decisions                         |
| `minHoldSeconds` | 0.5 s   | a new owner keeps the ball at least this long   |
| `temperature`    | 0.15    | softmax temperature over utility                |
| `scoring`        |         | the candidate scores' configuration             |

The system rejects an interval below one tick, a negative or non-finite hold and
a temperature that is not positive and finite.

## What this is not

A player on the ball only passes or keeps the ball: no dribble, no shot, no
turning to look for options. Off-ball players do not decide yet; they hold their
position or chase a free ball (see [reception](reception.md)).
