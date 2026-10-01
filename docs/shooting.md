# Shooting

A [shot decision](shot-decisions.md) says where a player aims. Shot execution
says what he makes of it: how the ball leaves his foot, what it meets on its way
— a defender, the keeper, the frame of the goal — and what it has become when it
is over: a goal, a save, a block or a miss. The strike lives in `sim-match`
(`shooting.hpp`), the goal frame in `goalFrame.hpp`, and the
[ball system](ball-movement.md) puts them together.

```text
ShotIntent   → strike        → flight → body · frame · keeper · line → ShotResolved
 pendingAction  executeShot()   ballAfter()  deflectShot()  findWoodworkHit()   GoalScored
```

## The strike

The ball system strikes a pending shot in the step after it was decided, if the
shooter still has the ball. The ball leaves the grass at his feet on the flight
that brings it to the aimed height at the aimed point of the goal line:
`launchVerticalVelocity()` solves the closed-form flight of
[ball movement](ball-movement.md) for the vertical speed that does it, at the
speed along the ground the decision chose (`ShotIntent::speed`). Such a flight
rises and falls once and does not touch the ground on the way.

- A shot aimed at the grass, or below it, is struck along it and rolls.
- The ball leaves no steeper than `maxLaunchSlope` (1, which is 45 degrees). A
  target that would need more — the top corner from two meters — gets the
  nearest the shooter can do.

That is the strike of a player who makes no mistake. A real one does, and
`executeShot()` draws his error from the `kExecution` stream, always six draws:

| Error          | Size                                         | Skill                     |
|----------------|----------------------------------------------|---------------------------|
| across the goal | triangular, half-width `spread`             | `shotAccuracy`            |
| up and down    | triangular, half-width `spread`              | `shotAccuracy` and `shotTechnique` |
| pace           | uniform, up to `speedError` (5 %) of the speed | `shotTechnique`         |
| spin           | uniform, up to `spinError` (15 rad/s)        | `shotTechnique`           |

```text
spread = (spreadAtZero + spreadPerMeter · distance) · errorFactor
```

with `distance` from the ball to the aimed point: 0.2 m plus 4 cm per meter,
the spread the [decision](shot-decisions.md#the-spread) judges the shot by. Each
skill scales its errors by `skillErrorFactor(skill) = 0.1 + 1.8 · (1 − skill)`:
a tenth at 1, exactly as given at 0.5, almost twice at 0. The aim strays along
the goal line and in height, and the launch is solved for the point it strays
to — so **accuracy** decides where the ball goes and **technique** how cleanly
it is struck: whether it is skied or scuffed, how hard, and with what spin.
However badly struck, a shot keeps between half and one and a half times its
intended pace.

A clean strike carries topspin, `topspin` (30 rad/s) times the shooter's
technique. There is top- and backspin only: it decides how the ball comes off
the grass when it bounces. Curling shots and shot types — the chip, the placed
shot — need a sidespin the ball does not have yet.

### What widens the error

`shotConditions()` reads four things off the true state, each in [0, 1], and
`shotErrorFactor()` multiplies them into the factor above — 1 for a player in
the clear, standing or running at his shot, on his strong foot, with a settled
ball:

| Condition   | What it is                                                                 | At its worst |
|-------------|----------------------------------------------------------------------------|--------------|
| `pressure`  | the nearest opponent, as for a pass (`passPressure()`)                     | × (1 + `pressureErrorFactor`) |
| `imbalance` | how much of his top speed the shooter moves at across or away from his shot | × (1 + `balanceErrorFactor`) |
| `weakFoot`  | the target lies on the side of his weaker foot                             | × (1 + `weakFootErrorFactor` · (1 − `weakFootAccuracy`)) |
| `unsettled` | he has only just received the ball, and it came fast                       | × (1 + `unsettledErrorFactor`) |

Every factor is 1 by default, so each condition doubles the errors at its
worst, and all four together make them sixteen times as wide.

- **Balance.** The part of his velocity that points along the shot costs
  nothing: running onto the ball is how a shot is struck. What he moves at
  sideways or backwards, over his `maxSpeed`, is his imbalance.
- **Foot.** A player has a `strongFoot`, right by default. A target more than
  `weakFootSide` (0.3, about 17 degrees) to the side of the other foot is struck
  with that one — for a right-footed player a target to his left, where left is
  the side of growing pitch y for a player facing growing pitch x. A
  `weakFootAccuracy` of 1 is a two-footed player, 0 doubles his errors, and the
  default 0.5 makes them half as wide again.
- **The ball.** A ball received within the last `unsettledSeconds` (1 s) is not
  settled: fully unsettled the moment it arrives at `unsettledBallSpeed`
  (20 m/s) or faster, less the slower it came and the longer he has had it. A
  ball he lost and won back since is no longer the one he received.

## On the way

A shot is **open** from the strike until its outcome is recorded;
`MatchState::lastShot()` holds it. While it is open, three things can happen to
the ball, and none of them ends the shot.

**A body in the way.** A shot at least `deflectionSpeed` (10 m/s) fast is not
taken at the feet by an outfield player, teammate or opponent. It comes off
him: within `blockRadius` (0.5 m) of its way, where the ball is no higher than
`blockReach` (1.8 m), `deflectShot()` sends it on with a share of its speed
between `minDeflectedSpeed` and `maxDeflectedSpeed` (0.1 to 0.9), turned up to
`deflectionSpread` (1 m to the side per meter along) off its line and lifted by
up to `deflectionLift` (3 m/s) — three draws from the `kExecution` stream. The
player becomes the ball's last touch, so a ball he puts behind his own goal
line is a corner. A ball that keeps less than `blockedBelow` (half) of its
speed was *blocked*; the rest of the tick it flies on from where it was hit.
A slower shot, and every other ball, is received as it always was
([reception](reception.md)).

**The keeper.** The keeper of the goal a shot attacks does not take it like
any ball. He reacts, dives, and where the ball passes him he holds it, parries
it or is beaten ([shot stopping](shot-stopping.md)). A ball he parries flies on
like a deflected one, with him as its last touch; the shot stays open, and was
`saved` if he parried it on its way in.

**The frame.** Posts and crossbar are round bars of `radius` (0.06 m): the
posts stand on the goal line at the posts of the
[pitch geometry](match-geometry.md), the crossbar lies on top of the goal's
height. The ball is a sphere of `kBallRadius` whose lowest point is its height.
`findWoodworkHit()` finds the first moment of a tick the two touch while the
ball moves into the bar, and the ball leaves with its speed into the bar
mirrored and scaled by `restitution` (0.5), the rest kept and its spin gone: it
comes back into play, goes out, or goes in off the post. The ball system
follows the rebound for the rest of the tick. This holds for every free ball,
not only for shots.

## Goals

A ball that leaves the pitch stops on the line where it crossed it. If that is a
goal line, between the posts and no higher than the crossbar's underside, it is
a **goal** — for shots, passes and deflections alike:

- The goal counts for the side attacking that end; the state's `score()` goes
  up by one and `lastGoal()` remembers it.
- The **scorer** is the shooter of that side's open shot, even if the ball went
  in off a defender. Without such a shot it is whoever touched the ball last —
  an **own goal** if he plays for the other side — and nobody for a ball no
  player had touched.
- The **assist** goes to the teammate whose pass the scorer last received, if
  that is how he came by the ball: `lastReception()` remembers whose pass a
  controlled ball was. A loose ball or an interception earns none, whoever the
  last pass was meant for.
- `GoalScored` records all of it with the score it makes.

With [restarts](restarts.md) enabled, the side that conceded kicks off from
the centre spot, with both sides [lined up](restarts.md#the-kickoff) in their
own halves.
Otherwise the ball stays on the line, as every ball that leaves the pitch does.

## Outcomes

Every shot gets exactly one `ShotResolved`, at the first of these:

| The ball                          | Outcome                                                           |
|-----------------------------------|-------------------------------------------------------------------|
| crosses a goal line in the frame  | `goal`                                                            |
| leaves the pitch anywhere else    | `offTarget`; `saved` if the keeper parried it on its way in; `blocked` if it came off an outfield player |
| comes to rest                     | the same                                                          |
| is taken by the opposing keeper   | `saved` if it was on its way into the goal or he had parried it so before, `offTarget` if on its way past; `blocked` if it had come off an outfield player |
| is taken by an opposing outfield player | `blocked`, or `saved` if the keeper had parried it on its way in |
| is taken by the shooter's own side | `offTarget`, `saved` or `blocked` as when it leaves the pitch    |

A shot **on target** is a `goal` or `saved`. Whether the keeper saved a shot is
asked of the flight he interrupted: `predictGoalLineCrossing()` follows the
ball to the goal line and the frame says whether it would have gone in.

## Events

All name the shot by its `shooter` and the tick it was struck in, `shotTick`, so
an analysis can put a chance together from its events
([match events](match-events.md)):

| Event             | Carries                                                                        |
|-------------------|--------------------------------------------------------------------------------|
| `ShotAttempted`   | where from, the aimed point and height, the point and height the ball really left for (`struckAt`, `struckHeight`), its speed, and the goal's distance and opening |
| `ShotDeflected`   | the player it came off, where and how high, and whether that blocked it         |
| `ShotHitWoodwork` | the part of the frame — `postAtMinY`, `postAtMaxY`, `crossbar` —, where and how high |
| `ShotResolved`    | the outcome: `goal`, `saved`, `offTarget` or `blocked`                          |
| `GoalScored`      | the side, the scorer, the assist, whether it was an own goal, and the score     |

A ball taken from a shot, or from a deflection of one, is a `LooseBallRecovered`,
never a pass received or intercepted.

## Configuration

`ShotConfig` is the `shooting` part of `MatchConfig`, and of every replay:

| Field                  | Default  | Meaning                                                   |
|------------------------|----------|-----------------------------------------------------------|
| `spreadAtZero`         | 0.2 m    | an average shooter's spread at the goal line              |
| `spreadPerMeter`       | 0.04     | and how much it grows per meter to the aimed point        |
| `speedError`           | 0.05     | how much harder or softer an average strike is            |
| `maxLaunchSlope`       | 1.0      | the steepest a ball leaves the foot: vertical over ground speed |
| `topspin`              | 30 rad/s | the topspin of a clean strike at perfect technique        |
| `spinError`            | 15 rad/s | how far an average strike's spin is off                   |
| `pressureErrorFactor`  | 1.0      | how much full pressure widens the errors                  |
| `balanceErrorFactor`   | 1.0      | how much a shot fully off balance does                    |
| `weakFootErrorFactor`  | 1.0      | how much a weak foot without any accuracy does            |
| `unsettledErrorFactor` | 1.0      | how much a fully unsettled ball does                      |
| `weakFootSide`         | 0.3      | how far to the weak side a target is struck with that foot |
| `unsettledSeconds`     | 1 s      | how long a received ball takes to settle                  |
| `unsettledBallSpeed`   | 20 m/s   | a ball received this fast is fully unsettled              |
| `deflectionSpeed`      | 10 m/s   | a shot at least this fast comes off outfield players      |
| `blockRadius`          | 0.5 m    | how far from the ball's way a body deflects it            |
| `blockReach`           | 1.8 m    | and up to which height                                    |
| `minDeflectedSpeed`    | 0.1      | the least share of its speed a deflected ball keeps       |
| `maxDeflectedSpeed`    | 0.9      | the most                                                  |
| `blockedBelow`         | 0.5      | below this share the deflection was a block               |
| `deflectionSpread`     | 1.0      | how far a deflection turns the ball off its line          |
| `deflectionLift`       | 3 m/s    | how much it lifts it                                      |

`WoodworkConfig` is the `woodwork` part:

| Field         | Default | Meaning                                            |
|---------------|---------|----------------------------------------------------|
| `radius`      | 0.06 m  | how thick posts and crossbar are, from their axis  |
| `restitution` | 0.5     | the share of its speed into the frame a ball keeps |

`validate()` rejects values outside their rules; see `shooting.hpp` and
`goalFrame.hpp`. The players' side of it is `PlayerAttributes`
([match state](match-state.md)): `shotAccuracy`, `shotTechnique`, `strongFoot`
and `weakFootAccuracy`.

## Tests and guardrails

`tests/unit/sim-match/shootingTests.cpp` holds the strike — an exact shot
arrives at the aimed point and height, every error stays within its bounds and
follows its skill and its conditions —, `goalFrameTests.cpp` the rebounds off
posts and crossbar, and `shotOutcomeTests.cpp` what becomes of a shot in the
ball system: a goal, a post, the crossbar, a block, a deflected goal, a save by
a keeper standing set, a wide shot, a shot that stops short, and that every
shot has exactly one outcome.

The `clear-chance` [scenario](scenarios.md) checks the whole of it over 100
seeds: the first shot is a goal 55 to 90 times. At the time of writing an
average finisher scores 72 of them and hits the woodwork with 48 — he aims
0.3 m inside the post with a spread of 0.5 m, and the decision that sends him
there counts every ball inside the frame as on target, while a ball within
17 cm of a post's axis hits it. From eight meters the ball is past the keeper
before he has reacted.

## What this is not

The keeper's positioning and his shot stopping are their own systems
([goalkeeper](goalkeeper.md), [shot stopping](shot-stopping.md)). Outfield players do
not block on purpose — a body in the way is a body in the way — and nobody
heads a ball. The shot decision still judges a shot by a straight line and a
point-sized ball; it does not know the flight or the thickness of the frame.
There is no curl, no chip and no choice of how to strike the ball. A goal
counts and is kicked off, but no match ends with a result yet.
