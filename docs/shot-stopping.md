# Shot stopping

A [shot](shooting.md) on its way to goal meets the keeper. He reacts, reads
where the ball will pass him, runs across if there is time, dives the rest of
the way, and when the ball passes him he holds it, parries it or is beaten;
then he is down for a moment before he can go again (GDD section 8.8). The
model lives in `sim-match` (`shotStopping.hpp`): the shot stopping system makes
the decision, the movement system moves him by it and the
[ball system](ball-movement.md) carries it out.

```text
strike or deflection → reaction  → decideDive()       → run, dive    → the ball passes his plane → touchesKeeper()? → executeSave()
                       reflexes     read (kAi), plan     bodyAt()       findPlanePassage()          feetAt(), handsAt()  hold or parry (kExecution)
                                                                                                                         → down, then up
```

## Facing a shot

A keeper **faces** a ball (`facingKeeper()`) while it is on a flight of the
open shot — struck, or deflected off an outfield player, not parried by him —,
on its way across his goal line, inside the frame or not, and either in the
air or at least `diveBelowSpeed` (8 m/s) fast. The keeper is the goalkeeper of
the side the shot is against; a side without a tactic has none. A slower ball
along the grass is no shot to dive for: he goes and picks it up like any ball
([reception](reception.md)), and it is `saved` if it was on its way in.

A ball he faces he does not take like any other ball: he meets it in **his
plane**, the upright plane through where he stood when he went, square to the
ball's way along the ground. A point of it is how far across from there —
positive to the left of the ball's way — and how high (`PlanePoint`). A free
ball travels in a straight line along the ground, so the ball passes his plane
where it comes closest to him. Until it has passed him, nobody else's
reception counts him in.

## Reacting and deciding

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

## Reach

While he runs his hands stay ready, `readyHeight` (1.4 m) above his feet
(`readyHands()`). Then they go in a straight line to his target at
`diveSpeed` (6 m/s) (`handsAt()`). They reach no farther from his feet than an
ellipse in his plane: `diveReach` (2.8 m) to either side along the ground and
`jumpReach` (2.5 m) up (`clampToReach()`). The ellipse is what makes the top
corner hard: with his body's reach, from where his feet are, he gets to a low
ball 3.3 m to his side, to one just under the crossbar about 1.1 m. And the
farther the corner, the longer his hands take to get there — which a run
before the dive can make up for, if the shot gives him the time.

He **touches** the ball if its centre comes within `bodyReach` (0.5 m) of the
line from his feet to his hands (`touchesKeeper()`): his body, his arms and his
hands. Before he has reacted — or if he left the ball — that is his body
standing set, so a shot at him is still in his way.

## His body

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

## Meeting the ball

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

  `skillErrorFactor()` is the shooters' ([shooting](shooting.md)): an average
  keeper holds a ball as often as it is easy, a keeper of handling 1 drops a
  tenth as many. An average keeper holds a 25 m/s shot straight at him a
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
before ([shooting](shooting.md#outcomes)).

## Events

`SaveAttempted` names the shot it answers by `shooter` and `shotTick`, and the
`keeper`, with where and how high the ball passed him. It is recorded when he
touches the ball, and when the ball was on its way into his goal and he did
not ([match events](match-events.md)):

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

## Configuration

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
players' side is `PlayerAttributes` ([match state](match-state.md)):
`keeperReflexes` and `keeperHandling` in [0, 1], 0.5 by default,
`keeperAnticipation`, by which he also judges a ball to come for
([goalkeeper](goalkeeper.md)), and `maxSpeed` and `acceleration` for his run.
How fast he dives, how far he reaches and how quickly he is up again are still
the same for every keeper; deriving them from a player's capabilities is #134.

## Tests

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

## What this is not

He does not anticipate a shot before it is struck, and he runs and dives from
a standing start across his plane only: he does not step toward the ball to
narrow the angle once it is struck. He does not smother a ball at the
shooter's feet, come out to block, punch a ball clear or claim a cross (#93,
#94). The dive moves his hands and puts his body under them; how his body
flies is not modelled beyond that.
