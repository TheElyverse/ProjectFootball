# Goalkeeper

The goalkeeper is a role of his own, not an outfield player who happens to
stand in goal (GDD section 8.8, implementation plan section 6.1, stage M4). The
player whose tactic slot holds `guardGoal` (`isGoalkeeper()`) takes up his
place in goal by its geometry, comes off his line for a ball played in behind
his defence when he judges he is first to it, and takes a ball in his own
penalty area with his hands. Stopping shots ([shot stopping](shot-stopping.md))
and what he does with the ball ([distribution](goalkeeper-distribution.md)) are
their own systems. The model lives in `sim-match`
(`goalkeeper.hpp`); the tactical movement system places him, the pursuit
system decides whether he comes, and reception gives him his hands.

```text
ball, phase, tactic → goalkeeperTarget()  → goalkeeperRegion()        → movement
                      the ideal place        + his placing error (kExecution)
free ball he could    callSweep()         → he chases it, or the next
come for            → his head start        teammate does
                      + his misjudgement (kAi)
```

## Where he stands

`goalkeeperTarget(state, playerIndex, phase, config, shotRange)` is where he
wants to be. It reads the true ball, like the outfield players' tactical
targets.

**On the bisector.** He stands on the bisector of the angle his two posts make
as seen from the ball, so the ball has as much goal to either side of him. The
bisector meets the goal line where it divides the goal in the ratio of the
ball's distances to the posts (the angle bisector theorem), and he stands
`depth` meters from there toward the ball. A ball out wide pulls him toward the
near post. Everything is vector arithmetic; no trigonometry, so the place is
the same on every platform.

**His depth** follows the situation:

| Situation                                              | Depth from the goal line |
|--------------------------------------------------------|--------------------------|
| the ball far up the pitch                              | `highDepthShare` (0.5) of his defensive line's depth, at most the depth of his penalty area |
| his own side on the ball                               | that high depth times `possessionDepthShare` (0.5): behind his defenders, never level with them |
| an opponent on the ball within `shotRange` of his goal | `lineDepth` (1.5 m)      |
| in between                                             | from `lineDepth` to the high depth, by the ball's distance from his goal over his defensive line's depth |

His defensive line is the outfield block's (`defensiveLineDepth()`, the same
line [desired regions](desired-region.md) scale the block into): the phase's
`lineHeight`, shifted with the ball by its `ballShift`. So the tactic's dials
move him: a high line takes him up with it, a deep block keeps him near his
goal. `shotRange` is the shot decision's `maxShotDistance` (25 m): the range a
shooter considers is the range the keeper treats as a threat.

He never stands beyond the ball, off the pitch, or wider than his posts by more
than he stands off his goal line: near his line with the ball out wide he
guards the near post, not the corner flag.

**Taking up his place.** `goalkeeperRegion()` is his desired region: the target
as its tactical target and, as its centre, where he actually goes — the target
off by a triangular draw along his bisector and one across it, spread by `positionErrorAlong` (1.5 m) and `positionErrorAcross`
(0.6 m) times `1 − keeperPositioning`. A keeper of positioning 1 stands exactly
on his target, an average one (0.5) up to 0.75 m off it, and a poor one opens
the near corner now and then. The draws come from the `kExecution` stream, four
every time the tactical movement system runs (`positioning.intervalTicks`),
whatever his skill. He runs straight to that centre: none of the outfield
costs — spacing, pressure, occupancy — apply to him.

**With his side on the ball** he decides like a teammate of the carrier, every
time an [off-ball decision](off-ball-movement.md) is due, but between two
options only (`generateKeeperSupportCandidates()`): holding his region, or
supporting the carrier from a spot inside his own penalty area with an open
lane. He never moves into space, runs in behind, goes wide or into a
halfspace. Without the ball he decides nothing in the tactical movement system:
he keeps goal, and the pursuit system decides whether he comes for a ball.

## Coming for a ball

The pursuit system sends one player per side after a free ball
([reception](reception.md)). When the goalkeeper would be his side's chaser of
a ball the opponent played last, and his interception point lies behind his
defensive line (`defensiveLineDepth()`), he first decides whether he comes; a
ball he would take in front of his defence he chases like anyone:

1. **Perceive.** His interception of the ball's predicted path and the
   opponents' earliest one, both from `findInterception()` — the same
   anticipation every chaser uses, with his hands' reach (`handsHeight`) on
   the part of the path inside his own penalty area.
2. **Judge.** His head start is the attacker's seconds minus his own. He
   misjudges it by `drawMisjudgement()`: a triangular draw from the `kAi`
   stream spread by `misjudgement` (0.8 s) times `1 − keeperAnticipation`,
   drawn once per ball — a ball is its last touch, so a deflection is a new
   one — and kept in his tactical state (`SweepJudgement`) while he follows it.
3. **Decide.** `callSweep()`: he comes if his head start plus his misjudgement
   reaches the threshold of his tactic's `goalkeeper.sweeping` dial,
   `sweepThreshold()` — linear from `cautiousMargin` (+0.5 s) at sweeping 0 to
   `boldMargin` (−0.2 s) at sweeping 1. A cautious keeper wants half a second
   on the attacker; a bold one goes even when the attacker is a fifth of a
   second closer. With no attacker able to reach the ball he always comes.
4. **Execute.** If he comes, he is the chaser and runs to his interception
   point; how he gets there and whether he takes the ball are movement and
   reception. If he stays, his side's next earliest player chases the ball, and
   the keeper keeps his place in goal.

A ball he stays home for he leaves. While he comes for one he judges it again
at every pursuit update, with the misjudgement he drew for it and the
threshold lowered by `sweepHysteresis` (0.1 s), so he abandons a run that has
become hopeless without running out and back. Once a teammate is earlier to
the ball than he is, he turns back too and leaves the ball to him. A ball his
own side played last he comes for like any other chaser.

Each judgement and each turn back he calls himself is reported as a
`SweepDiagnostic` with both arrival times, the misjudgement and the threshold; the
[decision trace](decision-trace.md) prints it:

```text
t=3 #8 sweeps (reaches the ball in 2.20 s, the first attacker in 4.20 s; misjudged by +0.00 s, needs +0.15 s)
```

## Hands

A goalkeeper who stands in his own penalty area, with the ball in it, at the
start of a tick reaches the ball with his hands: within `handsRadius` (1.2 m)
of him and up to `handsHeight` (2.2 m), against the feet's `controlRadius` and
`controlHeight` (1 m each) of everyone else (`ReceptionConfig`). That holds for
passes and loose balls alike; a shot at his goal he meets with a dive instead
([shot stopping](shot-stopping.md)). Outside his area he plays as an outfield
player, with his feet. A ball he takes with his hands he holds: nobody
challenges him for it, he throws or punts it, and once it has left his hands
he may not take it in them again until another player has touched it
([distribution](goalkeeper-distribution.md)). There is no back-pass rule and
no six-second rule yet.

## Recognising him

Opponents know who keeps goal from his shirt: defenders do not mark him and a
press does not block a lane to him by `keepsGoal()`, which is `isGoalkeeper()`
for a side with a tactic wherever he stands. Only a scripted side, which has no
roles, is read by position (within `kKeeperDepth`, 8 m, of its goal line).

## Configuration

`GoalkeeperConfig` is part of `MatchConfig` and of every replay; the hands are
part of `ReceptionConfig`.

| Field                  | Default | Meaning |
|------------------------|---------|---------|
| `lineDepth`            | 1.5 m   | how far off his line he stands against a shot |
| `highDepthShare`       | 0.5     | his depth far from the ball, as a share of his defensive line's |
| `possessionDepthShare` | 0.5     | the share of that depth he keeps with his own side on the ball |
| `positionErrorAlong`   | 1.5 m   | how far off his target a keeper of positioning 0 may stand along his bisector |
| `positionErrorAcross`  | 0.6 m   | the same across it |
| `cautiousMargin`       | 0.5 s   | the head start he needs at sweeping 0 |
| `boldMargin`           | −0.2 s  | the head start he needs at sweeping 1 |
| `sweepHysteresis`      | 0.1 s   | how far his margin may fall below the threshold before he turns back |
| `misjudgement`         | 0.8 s   | how far a keeper of anticipation 0 may misjudge his head start |

The tactic's dial is `principles.goalkeeper.sweeping` in [0, 1]
([tactic format](tactic-format.md)): pressing plays 0.8, possession 0.6,
counter 0.2 and the reference tactic 0.5. The player's are
`keeperPositioning` and `keeperAnticipation` in [0, 1], 0.5 by default
([match state](match-state.md)).

## Scenarios and tests

| Scenario             | Setup | Checked |
|----------------------|-------|---------|
| `keeper-arc`         | Home player 1 carries the ball across the pitch 27 m in front of away's goal, out of shooting range, and keeps it; away's outfield players neither press nor mark. | Over ten seeds away's keeper stands off his line, on the ball's side of the goal while it crosses, and once the ball has stopped within 0.75 m of the bisector. |
| `keeper-sweep-claim` | Home player 1 plays a through ball between away's centre backs to 12 m in front of away's goal; home's striker starts wide and far from it. | Over ten seeds the keeper's first call is to come, and he is the first to have the ball. |
| `keeper-sweep-leave` | The same through ball with home's striker a few meters ahead of it. | Over ten seeds the keeper stays home, never comes, stays in his area, and the striker has the ball. |

Away's keeper is a perfect judge in the two sweep scenarios
(`keeperAnticipation` 1), so whether he comes is the model's call. The
statistical side is a unit test: over a thousand draws, a keeper of
anticipation 0.2 misjudges balls a tenth of a second either side of his
threshold more than twice as often as one of 0.8, and one of 1 never does
(`tests/unit/sim-match/goalkeeperTests.cpp`,
`tests/acceptance/goalkeeperScenarioTests.cpp`).

## What this is not

What he does with the ball is [distribution](goalkeeper-distribution.md). He
does not command his area
for crosses, organise his defence or come for high balls in the air beyond what
his hands reach on the ground. The pursuit system's estimate asks a player to
reach the ball's point rather than come within his reach of it, so a call that
would flip as the ball runs past an attacker is kept by leaving a ball once.
