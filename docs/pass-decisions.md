# Pass decisions

A player on the ball decides, several times a second, whether and where to pass
or to shoot, or to hold, turn, carry or dribble. The decision reads only his own
[perception](perception.md), picks among his
[pass candidates](pass-candidates.md), his best [shot](shot-decisions.md) and
the best of each kind of his [carries](ball-carrying.md) with a seeded softmax,
and leaves the kick or the touch to execution ([passing](passing.md),
[ball carrying](ball-carrying.md#the-touch-the-ball-leaves-his-feet)). It lives
in `sim-match` (`passDecision.hpp`) and runs as the system
`makePassDecisionSystem()`.

## The pipeline

```text
perception → candidates and utilities → softmax choice → PassIntent  → execution
 (memory)     generatePassCandidates()     chooseOnBall()   ShotIntent    ball system
              generateShotCandidates()                      CarryIntent
              generateCarryCandidates()                     pendingAction
```

Every `intervalTicks` ticks (6: five times a second at 30 Hz), and in the tick
after a carrier got his touched ball back
([ball carrying](ball-carrying.md#when-he-decides)):

1. **Is there a decision to make?** Not while the ball is free, rolls ahead of
   its owner after a touch, or its owner already has an action pending.
2. **Hold.** A player keeps a ball he took less than `minHoldSeconds` (0.5 s) ago,
   measured from his last touch — a goalkeeper one he holds in his hands less
   than the distribution's `holdSeconds` (2 s). A player who owns the ball from
   the start of a state may pass at once, and so may one whose last touch was
   his own touch on in his active run.
3. **Options.** `generatePassCandidates()`, `generateShotCandidates()` and
   `generateCarryCandidates()` list and score his options from his memory. A
   goalkeeper adds his long balls, and from his hands throws no farther than
   `throwRange` ([goalkeeper distribution](goalkeeper-distribution.md)).
4. **Choice.** `chooseOnBall()` picks one of the valid passes, the best valid
   shot or the best valid carry of each kind. Without a valid option -- only
   with the ball in a goalkeeper's hands, since holding the ball at the feet is
   always valid -- he keeps the ball; a goalkeeper who has held it in his hands
   for `clearSeconds` (6 s) clears it
   ([goalkeeper distribution](goalkeeper-distribution.md#the-ball-in-his-hands)).
   A carrier who holds the ball, or carries it without gaining anything,
   [looks for an option](#without-a-valid-option).
5. **Intent.** The choice becomes his pending action — for a pass the target,
   planned speed, receiver and whether it is lofted, for a shot the aimed
   point, height and speed, for a turn, carry or dribble the touch. The ball
   system plays it in the next step.

## Without a valid option

A carrier who finds no valid option does not stand on the ball for good. He
looks around for one, and plays the best he has once he has looked long
enough. Only a goalkeeper holding the ball in his hands keeps his own rule.

The same holds for a carrier who has options but holds the ball instead, or
carries it without gaining anything -- less than `minCarryDistance` toward the
goal, no better shot, nobody bypassed ([ball carrying](ball-carrying.md#holding-on-and-getting-stuck)):
he looks around for a better option, and once stuck plays the ball. A carry
that gains something goes on, and his look around keeps its time through it.

**He looks over his shoulder.** Each decision without an option he looks at
the next of his look targets (`lookTargets()`, `carrierScan.hpp`) until the
next decision, 0.2 s and two [perception](perception.md) updates later. He
turns his head, not his body: the look is a direction of its own, his scan's
`gaze`, which perception uses instead of his facing while he has the ball.
His body and the ball in front of it stay as they are. The scan
(`CarrierScan` in his tactical state) belongs to his possession: it ends
when he plays the ball, and counts for nothing once he has lost it or
touched it again other than to carry it on.

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
side on a tie or without a grid. A side whose point lies closer to the ball
than the distribution's `minLongDistance` (25 m) gives way to the other, as
for a carrier at the near touchline as far up as the goal-line margin lets
him; if both do, the farther wins. The kick goes at the speed along the ground
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
| `carrying`       |         | how he weighs holding, turning, carrying and dribbling ([ball carrying](ball-carrying.md#configuration)) |

The system rejects an interval below one tick, a negative or non-finite hold,
stuck time or margin, a temperature that is not positive and finite, and
invalid scoring, carrying or distribution. It runs every tick, to follow a
touched ball and to decide when a carrier gets it back, but decides at the
interval otherwise.

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

A player on the ball kicks long only as a goalkeeper or when he has nothing
else. Where he looks ignores the opponents around him; shielding the ball from
a presser is his move in a [duel](duels.md). Off-ball players do not decide here; see
[off-ball movement](off-ball-movement.md) and [reception](reception.md).
