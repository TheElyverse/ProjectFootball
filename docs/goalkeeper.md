# Goalkeeper

The goalkeeper's own game: where he stands and when he comes for a ball, how he
stops shots, and what he does with the ball once he has it.

- [Positioning and sweeping](#positioning-and-sweeping)
- [Shot stopping](#shot-stopping)
- [Distribution](#distribution)

## Positioning and sweeping

The goalkeeper is a role of his own (GDD section 8.8, implementation plan
section 6.1, stage M4). The player whose tactic slot holds `guardGoal`
(`isGoalkeeper()`) takes his place in goal by its geometry, comes off his line
for a ball in behind his defence when he judges he is first to it, and takes a
ball in his own penalty area with his hands. [Shot stopping](#shot-stopping)
and [distribution](#distribution) are their own systems. The model is in
`sim-match` (`goalkeeper.hpp`): the tactical movement system places him,
pursuit decides whether he comes, and reception gives him his hands.

```text
ball, phase, tactic → goalkeeperTarget()  → goalkeeperRegion()        → movement
                      the ideal place        + his placing error (kExecution)
free ball he could    callSweep()         → he chases it, or the next
come for            → his head start        teammate does
                      + his misjudgement (kAi)
```

### Where he stands

`goalkeeperTarget(state, playerIndex, phase, config, shotRange)` is where he
wants to be. It reads the true ball, like the outfield players' tactical
targets.

**On the bisector.** He stands on the bisector of the angle his two posts make
as seen from the ball, so the ball has as much goal to either side of him. The
bisector meets the goal line where it divides the goal in the ratio of the
ball's distances to the posts (angle bisector theorem); he stands `depth`
meters from there toward the ball. A ball out wide pulls him toward the near
post. It is all vector arithmetic, no trigonometry, so the place is the same on
every platform.

**His depth** follows the situation:

| Situation                                              | Depth from the goal line |
|--------------------------------------------------------|--------------------------|
| the ball far up the pitch                              | `highDepthShare` (0.5) of his defensive line's depth, at most the depth of his penalty area |
| his own side on the ball                               | that high depth times `possessionDepthShare` (0.5): behind his defenders, never level with them |
| an opponent on the ball within `shotRange` of his goal | `lineDepth` (1.5 m)      |
| in between                                             | from `lineDepth` to the high depth, by the ball's distance from his goal over his defensive line's depth |

His defensive line is the outfield block's (`defensiveLineDepth()`, the same
line [desired regions](team-tactics.md#desired-region) scale the block into): the phase's
`lineHeight`, shifted with the ball by its `ballShift`. So a high line takes
him up, a deep block keeps him near his goal. `shotRange` is the shot decision's
`maxShotDistance` (25 m): the keeper treats as a threat what a shooter
considers.

He never stands beyond the ball, off the pitch, or wider than his posts by more
than he stands off his goal line: near his line with the ball out wide he
guards the near post, not the corner flag.

**Taking up his place.** `goalkeeperRegion()` is his desired region: the target
as its tactical target and, as its centre, where he actually goes — the target
off by a triangular draw along his bisector and one across it, spread by `positionErrorAlong` (1.5 m) and `positionErrorAcross`
(0.6 m) times `1 − keeperPositioning`. Positioning 1 stands exactly on the
target, an average keeper (0.5) up to 0.75 m off it; a poor one opens the near
corner now and then. Four draws from the `kExecution` stream per run of the
tactical movement system (`positioning.intervalTicks`), whatever his skill. He
runs straight to that centre; the outfield costs (spacing, pressure, occupancy)
do not apply.

**With his side on the ball** he decides like a teammate of the carrier, every
time an [off-ball decision](team-tactics.md#off-ball-movement) is due, but between two
options only (`generateKeeperSupportCandidates()`): holding his region, or
supporting the carrier from a spot inside his own penalty area with an open
lane. He never moves into space, runs in behind, goes wide or into a
halfspace. Without the ball he decides nothing in the tactical movement system:
he keeps goal, and the pursuit system decides whether he comes for a ball.

### Coming for a ball

The pursuit system sends one player per side after a free ball
([reception](ball.md#reception-interceptions-and-loose-balls)). When the goalkeeper would be his side's chaser of
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
at every pursuit update, with the same misjudgement and the threshold lowered
by `sweepHysteresis` (0.1 s); so he abandons a hopeless run without dithering.
Once a teammate is earlier to the ball, he turns back. A ball his own side
played last he chases like anyone.

Each judgement and each turn back he calls himself is reported as a
`SweepDiagnostic` with both arrival times, the misjudgement and the threshold; the
[decision trace](tooling.md#decision-trace) prints it:

```text
t=3 #8 sweeps (reaches the ball in 2.20 s, the first attacker in 4.20 s; misjudged by +0.00 s, needs +0.15 s)
```

### Hands

A goalkeeper who stands in his own penalty area, with the ball in it, at the
start of a tick reaches the ball with his hands: within `handsRadius` (1.2 m)
of him and up to `handsHeight` (2.2 m), against the feet's `controlRadius` and
`controlHeight` (1 m each) of everyone else (`ReceptionConfig`). That holds for
passes and loose balls alike; a shot at his goal he meets with a dive instead
([shot stopping](#shot-stopping)). Outside his area he plays as an outfield
player, with his feet. A ball he takes with his hands he holds: nobody takes
him on for it in a [duel](duels.md#duels-on-the-ball), he throws or punts it,
and once it has left his hands he may not take it in them again until another
player has touched it ([distribution](#distribution)). There is no back-pass
rule yet; holding the ball too long is the eight-second rule of
[restarts](match-engine.md#holding-the-ball-too-long).

### Recognising him

Opponents know who keeps goal from his shirt: defenders do not mark him and a
press does not block a lane to him by `keepsGoal()`, which is `isGoalkeeper()`
for a side with a tactic wherever he stands. Only a scripted side, which has no
roles, is read by position (within `kKeeperDepth`, 8 m, of its goal line).

### Configuration

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
([tactic format](tactics.md#tactic-file-format)): pressing plays 0.8, possession 0.6,
counter 0.2 and the reference tactic 0.5. The player's are
`keeperPositioning` and `keeperAnticipation` in [0, 1], 0.5 by default
([match state](match-engine.md#match-state)).

### Scenarios and tests

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

### What this is not

- He does not command his area or organise his defence. High balls in his
  area he contests in the air ([aerial duels](duels.md#the-keepers-hands)).
- Pursuit asks a player to reach the ball's point, not to come within reach of
  it; a call that would flip as the ball runs past an attacker is kept by
  leaving a ball once.

## Shot stopping

A [shot](on-ball.md#shooting) on its way to goal meets the keeper. He reacts,
reads where the ball will pass him, runs across if there is time and dives the
rest. When it passes him he holds it, parries it or is beaten; then he is down
for a moment (GDD section 8.8). The model is in `sim-match`
(`shotStopping.hpp`): the shot stopping system decides, the movement system
moves him, and the [ball system](ball.md#ball-movement) carries it out.

```text
strike or deflection → reaction  → decideDive()       → run, dive    → the ball passes his plane → touchesKeeper()? → executeSave()
                       reflexes     read (kAi), plan     bodyAt()       findPlanePassage()          feetAt(), handsAt()  hold or parry (kExecution)
                                                                                                                         → down, then up
```

### Facing a shot

A keeper **faces** a ball (`facingKeeper()`) while it is on a flight of the
open shot — struck, or deflected off an outfield player, not parried by him —,
on its way across his goal line, inside the frame or not, and either in the
air or at least `diveBelowSpeed` (8 m/s) fast. The keeper is the goalkeeper of
the side the shot is against; a side without a tactic has none. A slower ball
along the grass is no shot to dive for: he goes and picks it up like any ball
([reception](ball.md#reception-interceptions-and-loose-balls)), and it is `saved` if it was on its way in.

A ball he faces he does not take like any other ball: he meets it in **his
plane**, the upright plane through where he stood when he went, square to the
ball's way along the ground. A point of it is how far across from there —
positive to the left of the ball's way — and how high (`PlanePoint`). A free
ball travels in a straight line along the ground, so the ball passes his plane
where it comes closest to him. Until it has passed him, nobody else's
reception counts him in.

### Reacting and deciding

From the tick the ball was struck or deflected, the keeper needs his reaction
time before he does anything:

```text
reaction = slowestReaction − (slowestReaction − quickestReaction) · keeperReflexes
```

0.3 s at reflexes 0, 0.1 s at 1, 0.2 s for an average keeper. Then the shot
stopping system decides his dive, once per flight (`decideDive()`):

1. **Perceive.** He follows the ball to his plane through where he stands now
   with `predictBallPassage()` — the same flight the ball system moves it on —
   and knows when it will get there.
2. **Read.** He misreads where it passes him by a triangular draw across and
   one up, each spread by `readError` (0.8 m) times `1 − keeperAnticipation`:
   two draws from the `kAi` stream, always. A keeper of anticipation 1 reads
   it exactly.
3. **Plan.** He can run across his plane first and dive the rest. Of every run
   from none to all the way under the point he read, in sixteen steps, he
   takes the one that gets his body closest to that point by the time the ball
   arrives, the longest of equally good ones: with time to spare he runs
   until he can reach it from in front of him, with none he dives from where he
   stands. He runs from standing at his own `acceleration` up to his own
   `maxSpeed` (`runDistance()`, `runSeconds()`).
4. **Leave or go.** He leaves the ball — neither runs nor dives — if he reads
   it crossing his goal line farther than `wideMargin` (0.5 m) beside or above
   the frame, or if the closest his plan gets him is farther from it than
   `tryMargin()`: 1.5 m at anticipation 0 to 0.5 m at 1. Short of that he
   goes, hopeless or not; the better he reads the game, the less he goes for
   lost causes.

The dive (`KeeperDive`) is his: his tactical state keeps the flight it
answers — the player and tick of the touch that sent the ball on its way —,
the tick he went, his plane, how far across he runs and how long that takes,
where he sends his hands from there, when the ball reaches him and how long he
will then be down. A deflection is a new flight, and he reacts to it anew —
unless he is still busy with his dive.

### Reach

While he runs his hands stay ready, `readyHeight` (1.4 m) above his feet
(`readyHands()`). Then they go in a straight line to his target at
`diveSpeed` (6 m/s) (`handsAt()`). They reach no farther from his feet than an
ellipse in his plane: `diveReach` (2.8 m) to either side along the ground and
`jumpReach` (2.5 m) up (`clampToReach()`). The ellipse makes the top corner
hard: with his body's reach he gets to a low ball 3.3 m to his side, to one
just under the crossbar about 1.1 m. The farther the corner, the longer his
hands take; a run before the dive makes up for that if there is time.

He **touches** the ball if its centre comes within `bodyReach` (0.5 m) of the
line from his feet to his hands (`touchesKeeper()`): his body, his arms and his
hands. Before he has reacted — or if he left the ball — that is his body
standing set, so a shot at him is still in his way.

### His body

A keeper busy with a dive is where it takes him (`bodyAt()`): the movement
system puts him under his hands, tick by tick, whatever his target — across
his plane as he runs, flying with his hands as he dives, and there, down, once
the ball has passed him. He is not turned. As a velocity the state keeps what
his legs allow, at most his `maxSpeed`.

He is **down** for

```text
recovery = (standingRecovery + (stretchRecovery − standingRecovery) · stretch) · (1 − reflexRecoveryShare · keeperReflexes)
```

after the ball reaches him: 0.3 s standing set to 1 s at full stretch, a third
shorter for a keeper of reflexes 1. `stretch` is how far his hands are from
ready above his feet, as a share of `diveReach`. A keeper who did not react in
time, or left the ball, is down for a standing keeper's recovery once it has
passed him. Busy with a dive or down (`isDiving()`), he takes no ball, the
pursuit system sends the next teammate after a free one, and he answers no
new flight. Then he is up and plays on like before: he can gather his own
parry, a ball that rebounds off the post, or one that got past him and
slowed, and he faces a shot again once a ball is on its way to him.

### Meeting the ball

The ball system moves a free ball as it always does. Where the ball passes the
plane of the keeper facing it within the tick (`findPlanePassage()`), and no
one else gets to it before, he meets it: with his dive, or standing set where
he is if he has not reacted yet or left it.

- **Out of reach.** It does not touch him and flies on as if he were not
  there.
- **Touched.** `executeSave()` decides, with four draws from the `kExecution`
  stream, always, whether he holds it. His chance is the ball's ease — how far
  it is below `catchableSpeed` (40 m/s), times how far he is from full stretch
  (`stretchOf()`) — with what it lacks widened by his handling:

  ```text
  ease  = (1 − speed / catchableSpeed) · (1 − stretch)
  catch = 1 − (1 − ease) · skillErrorFactor(keeperHandling)
  ```

  `skillErrorFactor()` is the shooters' ([shooting](on-ball.md#shooting)): an average
  keeper holds a ball as often as it is easy, a keeper of handling 1 drops a
  tenth as many. A ball at `catchableSpeed` or faster nobody holds, whatever
  his handling. An average keeper holds a 25 m/s shot straight at him a
  little more than one time in three.
- **Caught.** He has the ball, at his feet where his dive took him, like any
  player who gains control of a free ball, and he is up at once.
- **Parried.** The ball comes off his palm. The palm faces back the way the
  ball came, tilted toward where he stretched to from his feet: by `parryTilt`
  (1.5) at full stretch across, upward only for a ball above his ready hands,
  and by up to `parrySpread` (0.5) at random either way. The ball keeps between
  `minParrySpeed` and `maxParrySpeed` (0.2 to 0.5) of its speed into the palm,
  mirrored, and all of its speed along it, and loses its spin. So a ball he
  meets in front of him goes back out into play, and one at full stretch on
  round the post or over the bar. He is its last touch, so a ball he puts
  behind his goal line is a corner. The ball flies on from there for the rest
  of the tick.

A parried ball is loose: whoever gets to it recovers it, it is no pass. The
shot stays open, and its outcome is `saved` if he parried it on its way into
his goal — unless it goes in all the same, which is a goal for the shooter.
A ball he caught was `saved` on its way in and `offTarget` on its way past, as
before ([shooting](on-ball.md#outcomes)).

### Events

`SaveAttempted` names the shot it answers by `shooter` and `shotTick`, and the
`keeper`, with where and how high the ball passed him. It is recorded when he
touches the ball, and when the ball was on its way into his goal and he did
not ([match events](match-engine.md#match-events-and-decision-diagnostics)):

| `result`          | What happened                                                       |
|-------------------|---------------------------------------------------------------------|
| `caught`          | he holds it                                                         |
| `parriedIntoPlay` | it comes off him and is not heading across his goal line            |
| `parriedBehind`   | it comes off him heading across his goal line beside or above the frame |
| `outOfReach`      | it was on its way in and did not touch him                          |

`parried…` is always his touch, however slight; a ball off an outfield player
is a `ShotDeflected`. Where a parried ball goes is asked of its flight as it
leaves him, so a `parriedIntoPlay` that goes in after all is followed by its
`GoalScored`.

### Configuration

`ShotStoppingConfig` is the `shotStopping` part of `MatchConfig` and of every
replay:

| Field                 | Default  | Meaning                                                     |
|-----------------------|----------|-------------------------------------------------------------|
| `slowestReaction`     | 0.3 s    | how long a keeper of reflexes 0 takes to react              |
| `quickestReaction`    | 0.1 s    | and one of reflexes 1                                        |
| `readError`           | 0.8 m    | how far a keeper of anticipation 0 may misread where the ball passes him |
| `readyHeight`         | 1.4 m    | his hands as he stands set                                  |
| `diveReach`           | 2.8 m    | how far to the side of his feet his hands reach along the ground |
| `jumpReach`           | 2.5 m    | how high they reach above his feet                          |
| `diveSpeed`           | 6 m/s    | how fast his hands go once he dives                         |
| `bodyReach`           | 0.5 m    | how close the ball's centre must come to his body and arms  |
| `catchableSpeed`      | 40 m/s   | a ball this fast he never holds                             |
| `minParrySpeed`       | 0.2      | the least share of its speed into his palm a parry keeps    |
| `maxParrySpeed`       | 0.5      | the most                                                    |
| `parryTilt`           | 1.5      | how far his palm turns toward his stretch at full stretch   |
| `parrySpread`         | 0.5      | and how far at random                                       |
| `carelessTryMargin`   | 1.5 m    | how far beyond what he gets to a keeper of anticipation 0 still goes for a ball |
| `carefulTryMargin`    | 0.5 m    | and one of anticipation 1                                   |
| `wideMargin`          | 0.5 m    | he leaves a ball he reads this far beside or above the frame |
| `diveBelowSpeed`      | 8 m/s    | a ball along the grass slower than this he picks up instead |
| `standingRecovery`    | 0.3 s    | how long he is down after a ball passed him standing set    |
| `stretchRecovery`     | 1 s      | and at full stretch                                         |
| `reflexRecoveryShare` | 1/3      | how much of that a keeper of reflexes 1 is spared           |

`validate()` rejects values outside their rules; see `shotStopping.hpp`. The
players' side is `PlayerAttributes` ([match state](match-engine.md#match-state)):
`keeperReflexes` and `keeperHandling` in [0, 1], 0.5 by default,
`keeperAnticipation`, by which he also judges a ball to come for
([goalkeeper](#positioning-and-sweeping)), and `maxSpeed` and `acceleration` for his run.
How fast he dives, how far he reaches and how quickly he is up again are still
the same for every keeper; deriving them from a player's capabilities is #134.

### Tests

`tests/unit/sim-match/shotStoppingTests.cpp` holds the model and shots on a
standard pitch:

- reflexes set his reaction and his recovery, anticipation how far he tries
  beyond his reach, and his run follows his speed and acceleration;
- a near corner is reached sooner than the far one, a low ball where the top
  corner is not, and before he moves only what comes at him touches him;
- anticipation sets how well he reads the ball; with time to spare he runs
  before he dives; he leaves a ball he cannot get near and one he reads going
  clearly wide;
- he decides his dive after his reaction and before the ball arrives, moves
  tick by tick, is down once it has passed him and up again after;
- a central shot is saved over twenty seeds, a shot into the top corner is out
  of reach and a goal, and the same shot into the bottom corner that beats him
  from 16 m he reaches from 30 m after a run;
- a save keeps possession with the ball where he met it, a parried shot
  rebounds into play and stays saved, he gathers his own parry once he is up,
  a shot parried at full stretch goes behind for a corner, and a slow ball
  along the ground he picks up without a dive.

`shotOutcomeTests.cpp` holds what the ball system makes of a keeper standing
set.

### What this is not

- He does not anticipate a shot before it is struck, and runs and dives only
  across his plane: no step toward the ball to narrow the angle.
- He does not smother a ball at the shooter's feet or come out to block (#93).
  Catching and punching high balls are
  [aerial duels](duels.md#the-keepers-hands).
- The dive moves his hands and puts his body under them; the body's flight is
  not modelled further.

## Distribution

A goalkeeper with the ball throws it, punts it, plays out with his feet or
kicks it long, and he takes the goal kick (GDD section 8.8). He decides like
every player on the ball ([pass decisions](on-ball.md#pass-decisions)) — same
pipeline, same softmax, same `kAi` stream — with his own options added to the
ground passes. His tactic decides how much he plays out and goes long. The
model is in `sim-match` (`distribution.hpp`); the
[ball system](ball.md#ball-movement) carries it out, and the
[restart system](match-engine.md#restarts) places the goal kick.

```text
ball in his hands or at his feet
  → options: ground passes (from his hands: throws, at most throwRange)
             + long balls: generateLongBallCandidates(), lofted
               utility + loftedBias = directnessBias(tactic's directness)
  → chooseOnBall() (kAi)
  → PassIntent{lofted}  → ball system: executePass() with loftedPassConfig() (kExecution)
                                       + loftedRise(): up on the flight model
```

### The ball in his hands

A keeper who takes the ball with his hands ([goalkeeper](#hands),
a caught shot, a high ball he holds in the air) **holds** it: the ball's
`held` is true. Holding it

- he stands where he took it: his movement target is gone;
- once he no longer runs he looks up the pitch, where his options are, not at
  the ball in his hands; otherwise he could face his own goal after a back
  pass, see no one and keep the ball for good;
- nobody takes him on for it in a [duel](duels.md#duels-on-the-ball);
- he keeps it at least `holdSeconds` (2 s) from the moment he took it, for
  his side to get into shape, against the half second (`minHoldSeconds`)
  anyone else keeps a ball he has just taken;
- he may not keep it longer than eight seconds (Law 12): with
  [restarts](match-engine.md#holding-the-ball-too-long) enabled, the other side
  then gets a corner. Still without a valid option after `clearSeconds`
  (6 s), he therefore **clears** it: he plays the pass with the highest
  utility of those rejected only as `kUnlikely` -- most often a long ball into
  a crowd -- and draws nothing more for it. Without such a pass he keeps it;
- his ground passes are **throws**, rolled out along the ground like a pass
  and no farther than `throwRange` (25 m);
- his lofted passes are **punts**.

Whatever he plays, the ball leaves his hands: his tactical state records the
tick (`handsReleased`), and **he may not take it in them again** until
another player has touched it (`hasReleasedBall()`). Until then he reaches it
as an outfield player does, with his feet, in his area as outside it; the
ball system forgets the release as soon as anyone else is the ball's last
touch. A restart forgets it too.

### Long balls

Only a goalkeeper kicks long. `generateLongBallCandidates()` lists a lofted
pass to every teammate he remembers with at least the scoring's
`minConfidence`, aimed at where he believes that teammate is now, from his
memory alone like every pass candidate:

- **The kick.** The ball comes down on the target after `flightSeconds` (3 s):
  its speed along the ground is `flightSpeed()` — the closed form of the
  flight under drag, `distance / (seconds · φ₁(drag · seconds))` — and its
  vertical speed `launchVerticalVelocity()` for that speed and distance
  ([ball movement](ball.md#flight)). Over 25 to 50 m the ball goes
  some ten meters up and comes down steeply, onto heads rather than feet.
- **The contest.** Where it comes down, the receiver and every remembered
  opponent race for it, each counted from no earlier than the ball arrives:
  an opponent's margin is `max(his time, flight) − max(receiver's time,
  flight)`. Two players who are both under the ball in time are even. His
  risk falls smoothly from 1 at a margin of −`contestMarginSeconds` (1 s)
  through ½ at 0 to 0 at +1 s, weighted by how sure the keeper is of him —
  the step of the [pass candidates](on-ball.md#scores) — and the ball
  survives every opponent: `interceptionRisk = 1 − Π (1 − risk)`.
- **The scores** are those of a ground pass otherwise
  (`scorePassCandidate()`): completion, progression and the receiver's
  pressure.
- **Rejections.** A long ball shorter than `minLongDistance` (25 m) is
  `kTooClose`, one that needs more than the passing's `maxLoftedSpeed`
  (28 m/s along the ground, about 53 m) `kOutOfReach`, one less likely than
  `minCompletion` `kUnlikely`.

The long balls join his ground passes in one list, ordered as
`orderPassCandidates()` orders every pass list; the choice among them is the
carrier's softmax.

A long ball flies on the [flight model](ball.md#flight); where it comes down it
is won in the air ([aerial duels](duels.md#aerial-duels-and-headers)) or at the
feet ([reception](ball.md#reception-interceptions-and-loose-balls)).

### The tactic's dial

`principles.goalkeeper.directness` in [0, 1] ([tactics](tactics.md#tactic-model)) says how
readily he goes long rather than playing out. It acts on the utility of his
long balls alone, as a part of their own:

```text
loftedBias = directnessWeight · (2 · directness − 1)
```

With the default `directnessWeight` of 0.5, directness 0 makes every long
ball half a point worse, 1 half a point better, and 0.5 judges it on its
merits. The presets: possession 0.15, reference 0.5, pressing 0.6, counter
0.85. A side without a tactic has no keeper and no long balls.

`passContributions()` reports the bias as `directness` (0 for every ground
pass), and the [decision trace](tooling.md#decision-trace) names it the reason when it
outweighs the rest.

### Execution

A lofted `PassIntent` (`lofted` true) is executed like a pass, with
`loftedPassConfig()`: the hardest kick `maxLoftedSpeed` instead of
`maxSpeed`, and both execution errors `loftedErrorFactor` (2) times as large,
grown by pressure as a pass's are. The ball then goes up at the vertical speed
that brings it down on the target at the intended speed, scaled by the same
pace error as its speed along the ground (`loftedRise()`) — 45 degrees should
the drag stop such a flight short. The same two `kExecution` draws as any
pass; no spin. A punt leaves the hands at the keeper's feet: there is no drop
kick height.

### The goal kick

A goal kick goes to the defending side's goalkeeper
([restarts](match-engine.md#restarts)). He takes it from his goal area: the ball lies on
`goalKickSpot()`, the front edge of the goal area level with where the ball
went out, but no wider than the goal area; he stands `carryDistance` behind it
facing up the pitch, at rest. He has it at his feet and decides like after
any other restart: ground passes or a long kick.

### Diagnostics

A goalkeeper's decision is a `DecisionDiagnostic` like everyone's: every
option with its scores, lofted or not, and his choice, with `held` saying
whether the ball was in his hands. The [decision trace](tooling.md#decision-trace)
names what he did — `throws to`, `punts to`, `kicks long to` or
`passes to` — with the utility's parts and the reason:

```text
t=0 #1 throws to #2 (utility 1.11: completion +1.00 progression +0.11 pressure -0.00 risk -0.00; estimated risk 0.00; 8 options, 13 observed) because completion -> received by #2 at t=58
t=0 #1 kicks long to #5 (utility 1.24: completion +0.86 progression +0.42 pressure -0.00 risk -0.04 directness +0.00; estimated risk 0.14; 12 options, 13 observed) because completion -> pending
```

### Configuration

`DistributionConfig` is the `distribution` part of `DecisionConfig`
([pass decisions](on-ball.md#pass-decisions)), and so of `MatchConfig` and every
replay:

| Field                  | Default | Meaning                                                     |
|------------------------|---------|-------------------------------------------------------------|
| `holdSeconds`          | 2 s     | he keeps a ball in his hands at least this long             |
| `clearSeconds`         | 6 s     | held this long without a valid option, he clears the ball   |
| `throwRange`           | 25 m    | the farthest he throws                                      |
| `minLongDistance`      | 25 m    | the shortest long ball                                      |
| `flightSeconds`        | 3 s     | how long a long ball is in the air                          |
| `contestMarginSeconds` | 1 s     | width of the contest-risk step where a long ball comes down |
| `directnessWeight`     | 0.5     | the lofted bias at directness 1, and minus it at 0          |

`validate()` rejects a non-finite value, a negative hold, clearance time,
shortest long ball or weight, a weight above `kMaxScoringWeight`, and a throw
range, flight time or margin that is not positive. The kick's own limits are `PassConfig`'s
`maxLoftedSpeed` (28 m/s) and `loftedErrorFactor` (2)
([passing](on-ball.md#passing)); the bias is the scoring's `loftedBias`
([pass candidates](on-ball.md#pass-candidates)), which the decision system sets for a
keeper and which is 0 in the configuration.

### Scenarios and tests

| Scenario           | Setup | Checked |
|--------------------|-------|---------|
| `keeper-build-up`  | Home's keeper holds the ball at his goal, his tactic's directness 0.1; his centre backs stand wide and free; away, without a tactic, stands a meter in front of his holding midfielder, in the lane from the keeper, and a meter off his wingers and striker. | Over ten seeds he throws, along the ground, to a centre back, who receives it; his hands are barred from the throw until the centre back has it. |
| `keeper-turned`    | `keeper-build-up` with the keeper facing his own goal, every option behind him. | Over ten seeds he turns up the pitch and throws to a centre back within five seconds. |
| `keeper-long-kick` | Home's keeper has the ball at his feet, directness 0.5; away stands in the lanes to his centre backs and his holding midfielder, a forward four meters to his side, out of the way of a long ball, its last two defenders deep; his wingers and striker wait beyond the longest ground pass. | Over ten seeds he kicks long to a winger or the striker, the ball goes more than five meters up, and somebody gets to it. |
| `goal-kick`        | Home player 1 plays the ball wide over away's goal line; restarts are on; away plays the reference tactic. | Over ten seeds away's keeper takes the goal kick, from `goalKickSpot()` in his goal area, the ball at his feet, and plays it from there. |

`tests/acceptance/distributionScenarioTests.cpp` holds the scenario checks;
`tests/unit/sim-match/distributionTests.cpp` that a long ball comes down on its
target after the flight time, that the dial biases long balls either way, the
rejections, that an opponent under a long ball in time makes it a fifty-fifty
and one far off does not, and that the bias adds to the utility as
`directness`. `duelTests.cpp` checks that nobody takes on a keeper holding
the ball, and `restartTests.cpp` where a goal kick is taken from.

### What this is not

- He does not drop the ball to his feet, roll it at a chosen pace, throw it
  overarm or drop-kick it; a punt leaves the ground, not his hands.
- No back-pass rule (he takes a teammate's pass in his hands), no rule against
  the goal kick taker playing the ball twice, and opponents need not leave the
  penalty area for a goal kick.
- Outfield players do not kick long; their lofted passes and crosses are to
  come (#94).
- A long ball aims at a teammate's position, not a space to run onto, and its
  flight time is the same for every distance.
