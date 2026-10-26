# Ball carrying

Between receiving the ball and releasing it, a player on the ball can hold it,
turn with it, carry it into space or dribble past an opponent (GDD section 8.5).
These are on-ball actions like passing and shooting: the
[pass decision](pass-decisions.md) generates them as candidates, scores them
from the carrier's [perception](perception.md) only and chooses among them and
the passes and the best shot with the same seeded softmax. How they are
played -- the touch, its error, the ball rolling ahead of him -- is the
execution half, in the ball system. Candidates and their scoring live in
`sim-match` (`carryCandidate.hpp`, `carrying.hpp`).

## The touch: the ball leaves his feet

A carrier does not glue the ball to his feet while he runs. Every turn, carry
or dribble starts with a **touch**: the ball leaves his feet as a short, slow
pass ahead of him and rolls freely until he gets to it again. He keeps
possession all along -- `BallState::owner` stays his, so team possession,
[tactical phases](match-phases.md), pressing and analytics see one possession --
but the ball moves on its own, and `BallState::touch` (`CarryTouch`) records the
tick of the touch and how many seconds after it he may play it again.

- **Who gets to it.** The ball rolls, leaves the pitch and goes into a goal as a
  free ball would. The first player to reach it takes it, by the
  [reception](reception.md) rules, with two differences: the carrier himself
  may take it once the touch's seconds have passed, without the reclaim delay,
  which is for passes, and his teammates leave it to him.
- **He gets it back.** Reaching it, he has it at his feet again
  ([possession](possession.md)), and the touch ends. His last touch stays the
  touch: getting his own ball back is no new possession.
- **An opponent gets it.** An opponent who gets to it first wins it: `BallWon`
  with the cause `looseTouch`, then `PossessionChanged`. A goalkeeper who
  reaches it in his own area takes it in his hands.
- **It leaves the pitch.** A touched ball that leaves the pitch is nobody's, and
  the [restart](restarts.md) system settles who plays on.

While the ball is ahead of him, no [challenge](pressing.md#winning-the-ball)
can take it from him -- there is nothing at his feet to challenge for. An
opponent wins it by getting to it, and a player
[pressing him](pressing.md#the-actions) runs at the ball instead of at him.

### The touch's error

The touch is a pass: `executePass()` sends the ball toward the touch's target
at its planned speed, both off by a random error from the `kExecution`
stream, grown by the [pressure](passing.md) on him. The error is the pass
error scaled by the touch's **error factor**, from `maxTouchError` (3) at a
`ballControl` of 0 down to `minTouchError` (0.5) at 1, linear in between; a
dribble's again by a factor from `maxDribbleError` (1.5) at a `dribbling` of 0
down to `minDribbleError` (0.5) at 1. A heavy touch -- too fast, or off line --
leaves the ball farther from where he meets it, and an opponent may get there
first: losing the ball is the same geometry as keeping it, never a roll of its
own. The speed error stays at most `0.9 / (1 + pressureErrorFactor)` (`0.45`
by default) however large the factor, so that pressure cannot stop the ball
dead or turn it round.

### Running with the ball

With the ball at his feet or rolling ahead of him, a player runs no faster than
`runningSpeed()` (`playerMovement.hpp`): his `maxSpeed` times a factor from
`BallPhysics::minCarrySpeedFactor` (0.8) at no ball control to
`maxCarrySpeedFactor` (0.95) at full control, linear in between. His
acceleration does not change. Every system that moves a player or predicts his
move -- the movement system, the ball at his feet, who gets to a ball first --
uses the same `stepPlayerMovement()` at that speed. A goalkeeper holding the
ball in his hands runs as fast as without it.

While the ball rolls ahead of him, he runs after it every tick: to where he
meets it, by `findInterception()` (`pursuit.hpp`) at his running speed with it.
Where that point lies within 45 degrees of where he runs, he runs on through it,
as far again as his run still goes from there, rather than braking for the
ball; elsewhere he brakes to take it.

## The options

`generateCarryCandidates()` lists a carrier's options with the ball at his
feet, from his memory only -- the opponents he remembers with at least
`minConfidence` (0.3), where he believes they are now. A goalkeeper with the
ball in his hands has none: he only distributes it. With carrying switched off
(`enabled`) there are none either.

### Hold

Holding the ball is always valid: he stays where he is, the ball at his feet.
Its utility is `holdWeight · (1 − pressure)`, where the pressure of the nearest
remembered opponent at distance `d` is `1 − d / pressureRadius` (6 m), at least
0. A player who holds the ball also [looks round](#holding-on-and-getting-stuck)
for an option.

### Carry and turn: where to

A carry runs along a **direction**, and the directions come from the
situation, never from a fixed set:

1. **Toward the goal**: the centre of the goal he attacks.
2. **Through the gaps**: the opponents within `gapRadius` (20 m), sorted round
   him by their direction from him -- half-planes and cross products, no
   trigonometry -- leave a gap between each two neighbours. He carries through
   the middle of each: the normalized sum of the two unit vectors, or its
   opposite for a gap wider than half a turn. One opponent leaves one gap, behind
   him.
3. **Away from pressure**: straight away from the nearest opponent.
4. **Into space**: toward the cell of the [pitch-control](pitch-control.md) grid
   his team controls best within the reach of a carry and at least
   `minCarryDistance` away -- ties to the cell farther up the pitch.

Of directions whose unit vectors have a dot product of at least
`sameDirection` (0.98), only the first counts.

**How far.** Along a direction he carries the ball as far as `horizonSeconds`
(2 s) at his running speed take him, checked meter by meter, no farther than
`pitchMargin` (1 m) inside the lines, and short of the first point an opponent
would get to, within `controlRadius`, less than `safetySeconds` (0.3 s) after
him. A carry shorter than `minCarryDistance` (2 m) is rejected as `kTooShort`.

**A turn** is a carry in a direction away from where he faces: its dot product
with his facing below 0. It goes `turnDistance` (3 m) at most, and only needs
to leave the spot by `minTouchDistance`. It takes time: `turnSeconds` (0.5 s)
standing, plus the time it takes him to stop his run along his facing, his speed
that way over his acceleration.

**The first touch** plays the ball to where he meets it: `touchSeconds` (0.5 s)
of running at his speed with the ball ahead of him, between `minTouchDistance`
(1 m) and `maxTouchDistance` (5 m), and no farther than the carry. Its speed is
the one that rolls it there in the time he needs to get there
(`estimateArrivalSeconds()`), from `d = v·t − a·t²/2`, at most the passing's
`maxSpeed`. A ball that would stop before then -- a slow turn, `d < a·t²/2` --
is played to stop where he meets it, `v² = 2·a·d`, rather than to roll past.
He may play it again after `touchShare` (0.5) of that time. Each
later touch is decided anew when he has the ball back.

### Dribble

A dribble is aimed at an opponent: at each remembered opponent no farther than
`dribbleRadius` (6 m) from the ball and standing in the direction he attacks
-- the unit vector toward him and the attacking direction with a dot product of
at least `dribbleCone` (0.5) -- but farther than `dribbleClearance` (2.5 m), on
either side of him: two candidates per opponent. Not for a goalkeeper.

The touch goes past the opponent: toward the point `dribbleBeyond` (1.5 m)
behind him, as far to his side as makes the ball's way pass him at
`dribbleClearance` -- for the opponent at distance `d`, `w = c·(d + b) /
sqrt(d² − c²)` to the side. A point off the pitch gives no dribble. His
`dribbling` shortens the time he plans to the ball, from `slowDribbleShare`
(1.2) times a carry's at 0 to `quickDribbleShare` (0.8) at 1, so a good
dribbler plays the ball past faster, and errs less.

Whether he gets past follows from the geometry: whether the opponent reaches
the ball before he does. There is no feint and no duel; the duels and tackles
of the P3 baseline will add them.

## Scoring

Every option but holding is scored by the same parts, each at its target:

| Part          | Value                                                         | Weight                     |
|---------------|---------------------------------------------------------------|----------------------------|
| `keep`        | the chance that no remembered opponent gets to the touched ball first | `keepWeight` (0.8) |
| `progression` | how much nearer the centre of the goal he attacks the target lies than the ball, over the pitch length | `progressionWeight` (0.8) |
| `space`       | how much more of the target than of the ball's spot his team controls ([pitch control](pitch-control.md)), 0 without a grid | `spaceWeight` (0.2) |
| `shot`        | how much better a shot from the target is than one from the ball, at least 0 | `shotWeight` (0.6) |
| `bypassed`    | the remembered outfield opponents between the ball and the goal, along the attacking direction, that the target leaves behind | `bypassWeight` (0.15) each |
| `risk`        | `1 − keep`, subtracted                                         | `riskWeight` (0.3)         |
| `turn`        | a turn's seconds, subtracted                                   | `turnCost` (0.6) per second |

`keep` is the pass's interception risk turned round: along the touch's way, the
smallest time each opponent has to spare reaching the ball before it gets
there, smoothed from 1 at `−lossMarginSeconds` (0.4 s) through 1/2 at 0 to 0 at
`+lossMarginSeconds`, weighted by how sure he is of the opponent, and combined
over all of them. A candidate with `keep` below `minKeepChance` (0.35) is
rejected as `kUnlikely`.

**Progression toward the goal**, not toward its line: a carry along the goal
line into the corner gains nothing, so a player does not wander off into it.

**The shot** from a point is the goal's opening there (`goalOpening()`) over
`fullOpening` (0.6), at most 1, falling linearly with the distance to the goal's
centre to 0 at `shotDistance` (25 m). It counts as an improvement over the ball's
spot: near goal, a carry that opens the angle is worth more, and one that only
keeps it nothing. The shot candidates themselves decide at the next touch.

`carryContributions()` returns the weighted parts, whose sum is the utility bit
for bit; `dominantContribution()` names the largest.

### The situation shifts the weights

A side with a [tactic](tactics.md) scores its carries for the phase its team is
in with the ball, by `carryScoringForPhase()`: each weight times its phase
factor.

| Phase                | Factors                                   | Effect                               |
|----------------------|-------------------------------------------|--------------------------------------|
| build-up             | risk ×1.5                                 | no carry that could lose the ball near his own goal |
| progression          | none                                      |                                      |
| final third          | shot ×2                                   | carries that open a shot              |
| attacking transition | progression ×2, bypassed ×2, risk ×0.8    | the counter: ground and opponents left behind |

Then `carryScoringForRisk()` scales the scoring by the phase instruction's
`passingRisk` `r`, as for [passes](pass-decisions.md#passing-risk):
progression `0.5 + r` times, risk `1.5 − r` times, and `minKeepChance` `1.3 −
0.6 r` times, at most 1. At `r = 0.5` nothing changes. A side without a tactic
scores with factors of 1.

## Choosing

`chooseOnBall()` adds the carries to the softmax over the valid passes and the
best valid shot (see [pass decisions](pass-decisions.md#seeded-softmax)): of the
valid carries, the best of each kind competes -- hold, turn, carry and dribble
-- not every direction, as only the best zone of the goal does for shots.
`orderCarryCandidates()` puts valid candidates first by utility, ties by kind
and subject.

The choice becomes a `CarryIntent` -- the touch's target, speed, seconds and
error factor -- his pending action, which the ball system plays in the next
step if he still has the ball at his feet. A turn, carry or dribble also starts
or continues his run (`CarryRun`, in his tactical state): its kind and target,
the opponent he dribbles past, when it began, his last touch and when he got the
ball back. A run of the same kind carries on from when it began; a dribble
always begins anew. The run belongs to his possession: `activeCarry()` ignores
it once he has lost the ball or anyone else has touched it. Holding the ball
stops him where he is.

### When he decides

He decides only with the ball at his feet: at the decision interval, and in the
tick after he got his touched ball back, whatever the interval. While it rolls
ahead of him there is nothing to decide -- he cannot pass a ball three meters
in front of him. His own touches do not make him a new owner: the hold time of
a ball he has just taken does not apply while his run is active.

### Looking ahead

While the ball rolls ahead of him he orients himself for his next decision
(GDD section 8.5): [perception](perception.md) looks along his run's gaze
instead of his facing. Until the touch's seconds have passed he looks at his
run's focus -- his best pass's receiver when he chose, or the goal if his best
shot was better, otherwise the carry's target --, then at the first of his
[look targets](pass-decisions.md#without-a-valid-option), and back at the ball
once he is `lookBackSeconds` (0.3 s) from meeting it.

### Holding on and getting stuck

Holding the ball, or carrying it without gaining anything -- less than
`minCarryDistance` toward the goal, no better shot, nobody bypassed -- he
[looks round for an option](pass-decisions.md#without-a-valid-option), a better
one if he has any, and once stuck for `stuckSeconds` plays his best pass or kicks
the ball long either way. Carrying it on with a gain, his look round keeps its
time: it ends only when he plays or loses the ball. So a player who keeps
gaining ground is never stopped, and one who goes round in circles is.

## Events and diagnostics

| Event              | Fields                                   | Recorded when                        |
|--------------------|------------------------------------------|--------------------------------------|
| `DribbleAttempted` | `carrier`, `opponent`, `from`, `target`  | the first touch of a dribble leaves his foot |
| `DribbleEnded`     | `carrier`, `opponent`, `outcome`, `position` | he gets the ball back -- `beaten` if it is then farther up the pitch than the opponent, `kept` otherwise -- or loses it: `lost`; none if his touch goes in |
| `BallWon`          | ... `cause`                              | `looseTouch` for an opponent who got to the touched ball |

No event marks a single touch: there are hundreds a match, and the decision
trace shows them.

The [decision diagnostic](match-events.md#decision-diagnostics) holds every
carry candidate with its scores and the weights they were scored with, and the
outcomes `kHeld`, `kTurned`, `kCarried` and `kDribbled` with the index of the
chosen carry; its `target` is the carry's target. The
[decision trace](decision-trace.md) prints the choice with its parts and why.

## Configuration

`CarryScoringConfig` is part of `DecisionConfig` (`decisions.carrying`) and of
every replay; `validate()` rejects values outside their ranges.

| Field               | Default | Meaning                                                |
|---------------------|---------|--------------------------------------------------------|
| `enabled`           | true    | players carry the ball at all                          |
| `minConfidence`     | 0.3     | opponents remembered with less are ignored             |
| `horizonSeconds`    | 2 s     | the longest carry he plans, at his running speed       |
| `minCarryDistance`  | 2 m     | a shorter carry is none                                |
| `gapRadius`         | 20 m    | opponents this close make the gaps                     |
| `sameDirection`     | 0.98    | directions this close are one                          |
| `pitchMargin`       | 1 m     | carries end this far inside the lines                  |
| `safetySeconds`     | 0.3 s   | a carry ends where an opponent gets there this soon after him |
| `lossMarginSeconds` | 0.4 s   | the smoothing of the risk of losing the touched ball   |
| `touchSeconds`      | 0.5 s   | the time to the ball a touch is planned for            |
| `minTouchDistance`, `maxTouchDistance` | 1 m, 5 m | a touch's shortest and longest       |
| `touchShare`        | 0.5     | he plays the ball again after this share of the planned time |
| `lookBackSeconds`   | 0.3 s   | he looks back at the ball this long before he meets it |
| `turnDistance`      | 3 m     | the longest turn                                       |
| `dribbleRadius`     | 6 m     | he dribbles past opponents this close                  |
| `dribbleCone`       | 0.5     | and this nearly in the attacking direction             |
| `dribbleClearance`  | 2.5 m   | the ball passes the opponent this far to his side      |
| `dribbleBeyond`     | 1.5 m   | and meets him this far behind the opponent             |
| `maxTouchError`, `minTouchError` | 3, 0.5 | the touch's error factor at no and at full ball control |
| `maxDribbleError`, `minDribbleError` | 1.5, 0.5 | a dribble's at no and at full dribbling |
| `slowDribbleShare`, `quickDribbleShare` | 1.2, 0.8 | a dribble's planned time over a carry's |
| `shotDistance`, `fullOpening` | 25 m, 0.6 | the shot value's range and full opening      |
| `minKeepChance`     | 0.35    | candidates less likely to keep the ball are rejected   |
| `pressureRadius`    | 6 m     | the pressure on him holding the ball                   |
| `keepWeight` ... `riskWeight` | 0.8, 0.8, 0.2, 0.6, 0.15, 0.3 | the weights of the scoring        |
| `holdWeight`        | 0.5     | holding the ball's worth without pressure              |
| `turnCost`, `turnSeconds` | 0.6, 0.5 s | a turn's cost per second, and its time standing |
| `buildUp`, `progression`, `finalThird`, `attackingTransition` | see above | the phase factors |

With them, `BallPhysics::minCarrySpeedFactor` and `maxCarrySpeedFactor` (0.8,
0.95), and the players' `ballControl` and `dribbling` in `PlayerAttributes`, in
[0, 1], 0.5 by default.

## Scenarios

The [carry scenarios](scenarios.md#the-carry-scenarios) show both: in
`carry-into-space` a carrier with nobody to pass to carries the ball into open
space touch after touch, and in `dribble-past-defender` he carries it at a
covering defender and takes it past him on one side.

## What this is not

Fatigue does not affect the touch yet: there is no energy model. There is no
feint, no shielding, no tackle and no duel: an opponent wins a touched ball only
by getting to it first, and a ball at the carrier's feet by the placeholder
challenge of [pressing](pressing.md#winning-the-ball). A carry does not aim to
open a passing lane to a teammate, and nobody carries the ball for a cross.
