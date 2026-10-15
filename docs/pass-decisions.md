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
   shot. Without a valid option he keeps the ball — a goalkeeper who has
   held it in his hands for `clearSeconds` (6 s) clears it
   ([goalkeeper distribution](goalkeeper-distribution.md#the-ball-in-his-hands)),
   any other carrier [looks for one](#without-a-valid-option).
5. **Intent.** The choice becomes his pending action — for a pass the target,
   planned speed, receiver and whether it is lofted, for a shot the aimed point, height and speed. The
   ball system plays it in the next step.

## Without a valid option

A carrier who finds no valid option does not stand on the ball for good. He
looks around for one, and plays the best he has once he has looked long
enough. Only a goalkeeper holding the ball in his hands keeps his own rule.

**He looks over his shoulder.** Each decision without an option he looks at
the next of his look targets (`lookTargets()`, `carrierScan.hpp`) until the
next decision, 0.2 s and two [perception](perception.md) updates later. He
turns his head, not his body: the look is a direction of its own, his scan's
`gaze`, which perception uses instead of his facing while he has the ball.
His body and the ball in front of it stay as they are. The scan
(`CarrierScan` in his tactical state) belongs to his possession: it ends
when he plays the ball, and counts for nothing once he has lost it or
touched it again.

**Where he looks.** The look targets come in two sides, taken from his
facing:

1. The **first side** is the one on which his turn toward the opponent's goal
   is shorter: a winger on the left at the opponent's goal line who faces it
   turns right, toward the box, one on the right turns left. A carrier who
   faces the goal or straight away from it turns toward the pitch's middle.
   On that side a forward -- his slot holds `runInBehind` -- looks at the
   opponent's penalty spot, everyone else at the centroid of the movement
   targets of his teammates there: where their tactic wants them, or where
   they stand without a target.
2. The **other side**: the centroid of his teammates' targets there.

A side without a teammate gives no target, and he keeps alternating between
the two while he has no option.

**He acts when stuck.** `stuckSeconds` (3 s) after the decision that first
found no option -- time for teammates to get free -- he plays the pass with
the highest utility of all he rejected. Without any pass at all he kicks the
ball long: lofted, to `clearanceTarget()`, `clearTouchlineMargin` (5 m) inside
a touchline, as far up the pitch as his longest kick reaches but no closer
than `clearGoalLineMargin` (10 m) to the opponent's goal line, on the side
where his team's [pitch control](pitch-control.md) is higher -- the nearer
side on a tie or without a grid. The kick goes at the speed along the ground
that brings it down there after the distribution's `flightSeconds`; his
longest is the one at the passing's `maxLoftedSpeed`. Neither choice draws a
number.

Should he stand on the ball anyway, an opponent nearby
[goes at him](pressing.md#a-carrier-who-stands-on-the-ball).

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
| `stuckSeconds`   | 3 s     | a carrier without a valid option looks for one this long |
| `clearTouchlineMargin` | 5 m | his long kick without any pass lands this far inside a touchline |
| `clearGoalLineMargin`  | 10 m | and no closer than this to the opponent's goal line |

The system rejects an interval below one tick, a negative or non-finite hold,
stuck time or margin, a temperature that is not positive and finite, and
invalid scoring or distribution.

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
carry, no turning his body with the ball; he only turns his head to look for
options, and kicks long only as a goalkeeper or when he has nothing else.
Where he looks ignores the opponents around him: shielding the ball from a
challenger comes with the duels. Off-ball players do not decide yet; they hold their
position or chase a free ball (see [reception](reception.md)).
