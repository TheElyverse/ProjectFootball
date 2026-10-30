# Ball

The ball on its own and between players: how it rolls, flies and bounces, who
owns it, and how a player gains control of a free ball.

- [Ball movement](#ball-movement)
- [Possession](#possession)
- [Reception, interceptions and loose balls](#reception-interceptions-and-loose-balls)

## Ball movement

A free ball rolls on the pitch, flies through the air and bounces on the grass.
The physics are pure functions in `sim-match` (`ballPhysics.hpp`). The system
`makeBallMovementSystem(BallMovementRules)` (`ballMovement.hpp`) runs them
every tick in the [match loop](match-engine.md#match-loop) and writes the
ball's position, velocity, height, vertical velocity and spin. A free ball knows
nothing about players; it moves the same with or without them. A controlled
ball follows its carrier on the ground ([possession](#possession)). The same
system plays pending passes, which free a controlled ball
([passing](on-ball.md#passing)), and gives a free ball to the first player who
reaches it ([reception](#reception-interceptions-and-loose-balls)).

### The ball's state

`BallState` holds the pitch plane and the third dimension:

| Field              | Unit  | Meaning                                              |
|--------------------|-------|------------------------------------------------------|
| `position`         | m     | where the ball is on the pitch                       |
| `velocity`         | m/s   | how it moves in the pitch plane                      |
| `height`           | m     | how far above the grass it is                        |
| `verticalVelocity` | m/s   | how fast it rises; negative while it falls           |
| `spin`             | rad/s | topspin, or backspin when negative                   |
| `sideSpin`         | rad/s | spin about the vertical axis: curls a flying ball left of its travel, right when negative |

A ball with no height and no vertical velocity lies or rolls on the grass.
`isAtRest()` holds when it does not move at all. The difference matters: a ball
at the top of its flight has no velocity in the pitch plane either, and is in
play. `isInFlight(ball)` is the counterpart; it is true for a ball just lifted
off the turf, before it has any height.

Spin is two signed numbers, not a vector: top- and backspin about the horizontal
axis across the direction of travel, which acts when the ball bounces, and
sidespin about the vertical axis, which [curves](#curve) its flight.

### Constants

`BallPhysics` holds them all; `validate()` rejects a set a ball cannot have.
All are part of `MatchConfig` and so of every
[replay](tooling.md#replay-format).

| Constant               | Default  | Meaning                                                     |
|------------------------|----------|--------------------------------------------------------------|
| `rollingDeceleration`  | 1.5 m/s² | speed lost per second while the ball rolls                  |
| `carryDistance`        | 0.5 m    | how far ahead of his feet a player carries the ball         |
| `gravity`              | 9.81 m/s²| pulls a flying ball down                                    |
| `airDrag`              | 0.33 1/s | fraction of its speed a flying ball loses per second        |
| `bounceRestitution`    | 0.6      | share of its vertical speed a bounce keeps                  |
| `bounceGrip`           | 0.8      | share of its speed along the ground a bounce keeps          |
| `spinTransfer`         | 0.2      | share of its spin a bounce spends driving the ball on       |
| `spinDecay`            | 0.4 1/s  | fraction of its spin a flying ball loses per second         |
| `restingVerticalSpeed` | 0.25 m/s | a bounce no faster than this leaves the ball down              |
| `magnusCurvature`      | 0.0008   | curvature of a flying ball's path, in rad/m, per rad/s of sidespin |

Rolling deceleration and gravity must be positive and finite; carry distance,
drag, spin decay, Magnus curvature and resting speed finite and not negative;
the three shares between 0 and 1.

### Rolling

The ball slows at a constant rate along its direction of travel. At speed `v`
it stops after `v / a` seconds and `v² / (2·a)` meters: at the default, a
10 m/s pass rolls about 33 m. `rollingDistance(speed, physics)` returns that
distance. Friction only takes speed away: a rolling ball never changes
direction, and it stops instead of rolling back.

The step is the exact solution, so the ball stops at the same point at any tick
rate, and a pass planner can compute that point without simulating.

### Flight

A ball off the ground, or with a vertical velocity, flies. Gravity pulls it
down, and the air takes speed from every component:

```
v(t) = v₀ · e^(-k·t)                          in the pitch plane
w(t) = (w₀ + g/k) · e^(-k·t) - g/k            upwards
```

for a drag `k` and gravity `g`; the height is the integral of `w`. Spin decays
the same way. The solution is exact for any span, so ticks do not change a
flight, and `SimCore::stableExp()` makes it bit-identical on every platform
([stable math](match-engine.md#stable-math)).

**Why a linear drag.** Quadratic drag needs a numerical integrator, which
depends on step size and rounding; [replays](tooling.md#replay-format) cannot
allow that. Linear drag has a closed form; the default gives a terminal speed
of `gravity / airDrag ≈ 30 m/s`, about right for a football.

**Why the terminal speed is not in the code.** For a tiny drag `g/k` overflows
and `1 - e^(-k·t)` cancels, sending a valid configuration to 270 m or `NaN`.
The step therefore uses the span `t · φ₁(k·t)` a unit speed covers and the drop
`g · t² · φ₂(k·t)` gravity adds, where

```
φ₁(x) = (1 - e^(-x)) / x        φ₂(x) = (e^(-x) - 1 + x) / x²
```

Neither divides by the drag. Below `x = 0.5` both come from their Taylor
series. `φ₁(0) = 1` and `φ₂(0) = ½` give the plain parabola, so one formula
covers no drag, a tiny drag and a real one.

### Curve

Sidespin bends a flying ball's path along a circle: the Magnus force pushes it
square to its travel, in proportion to spin and speed, so the path turns by the
same angle per meter however fast the ball goes. Its curvature is
`magnusCurvature · sideSpin` (`flightCurvature()`), positive turning left. A
curled shot at 40 rad/s bends along a circle of about 31 m radius, some 1.6 m
off its chord over 20 m.

The speed decays along the path exactly as on a straight line, so the path's
length over a span is the straight flight's distance, and `alongArc()` turns it
into the point and the direction it reaches:

```
offset    = d · sin(θ)/k + l · 2·sin²(θ/2)/k        θ = k · s
direction = d · cos(θ) + l · sin(θ)
```

for curvature `k`, the path's length `s`, the starting direction `d` and `l`,
`d` turned left. `2·sin²(θ/2)` stands for `1 − cos(θ)`, which would cancel for a
small angle, and curvature 0 is the straight line bit for bit. The sine and
cosine are `SimCore::stableSineCosine()` ([stable math](match-engine.md#stable-math)),
so a curving flight is closed-form, tick-rate independent and bit-identical on
every platform like any other.

Sidespin stays as it is in flight: a constant curvature is what keeps the path a
circle and the solution closed. A bounce keeps `1 − spinTransfer` of it; a ball
rolls straight, and whatever plays or stops the ball otherwise -- a player who
takes it, a pass, touch or header, a body, the woodwork, the keeper, the pitch
boundary -- takes it all.

`aimAlongArc(from, to, curvature)` is the other way round: the direction to
strike a ball so its arc reaches a point — off the straight line by half the
angle the path turns — and the arc's length. It eases a curvature that would
turn the path by more than a sixth of a circle to that.

### Bounce

Where the flight reaches the ground within a tick, the ball bounces:

- it keeps `bounceRestitution` of its vertical speed, upwards;
- it keeps `bounceGrip` of its speed along the ground;
- it spends `spinTransfer` of its spin driving itself on — topspin forward,
  backspin against the travel, never enough to turn the ball around. The spent
  spin is gone. A ball dropping straight down has no direction to drive along
  and keeps none;
- a bounce that leaves it rising no faster than `restingVerticalSpeed` (equal
  included) leaves it down; it rolls the rest of the tick. So a
  `bounceRestitution` of 0 settles the ball even with a `restingVerticalSpeed`
  of 0.

The tick is split at the moment of landing, so a bounce happens at its own time
and flights do not depend on the tick rate: at 30, 120 and 300 Hz they agree
within a micrometer, as the tests pin. Bisection finds the landing time — a
fixed 64 halvings, to the last bits of a double. At most eight bounces are
resolved per tick; after that the ball is left down.

### Predicting a landing

`predictBallLanding(ball, physics)` says where and when a flying ball comes down
and how high it gets on the way, without stepping the match: the same bisection
over the closed-form flight, curve included, bracketed by doubling a one-second horizon. It is
empty for a ball on the ground and for a ball without gravity, which never comes
down. Chasers use it through [reception](#reception-interceptions-and-loose-balls),
the goalkeeper too when he decides whether to come for a ball
([goalkeeper](goalkeeper.md#positioning-and-sweeping)).

It ignores the pitch boundary: it is the flight's own landing; the systems
decide what happens to a ball that leaves the pitch.

### Pitch boundary

A ball that crosses a touchline or goal line stops on the line at the crossing
point, on the ground, at rest, without spin — in the air as on the grass. The
ball is out of play and waits there for the
[restart](match-engine.md#restarts) system, which awards the throw-in, goal kick
or corner as a [set piece](set-pieces.md); a scenario without restarts leaves
it there. A ball that crosses a goal line between the posts and under the
crossbar is a goal; on the way, posts and crossbar can turn it away
([shooting](on-ball.md#shooting)). A ball rolling along or resting on a line is
on the pitch (`Pitch::contains()` includes the edges) and keeps rolling.

Within a tick a free ball travels along a straight line in the pitch plane, or
for a curling one along so little of its circle -- a few centimeters off the
chord at most -- that the chord stands for it: drag acts along its travel, and a
bounce only scales its speed. One crossing point therefore describes a whole
tick, however often the ball bounces in it.

Such a ball travels only part of the tick; `stepFreeBallTimed()` reports that
part, the fraction of the way to the line.
[Reception](#reception-interceptions-and-loose-balls) needs it to time a
contact on the shortened path and to know how high the ball crossed.

A ball that already lies off the pitch — possible only in a hand-built state —
plays on without this rule.

### Starting with a rolling ball

`makeSevenASideKickoff(pitch, ballVelocity)` puts the ball on the center spot
with the given velocity, at rest by default. A non-finite velocity is rejected
like any invalid state.

### What this is not

Only a [shot](on-ball.md#shooting) -- [curled](on-ball.md#shot-types) with
sidespin or not -- and a goalkeeper's long ball
([goalkeeper distribution](goalkeeper.md#distribution)) lift the ball. Every
other pass is a ground pass and leaves the foot without height, vertical
velocity or spin. Crosses and outfield lofted passes will fill the third
dimension further. Who wins a high ball is decided in the air
([aerial duels](duels.md#aerial-duels-and-headers)).

- Sidespin does not decay in flight, and a rolling ball does not curve.
- Players perceive the ball's position on the pitch, not its height.
- No air resistance while rolling, no wind.
- The ball hits only grass, goal frame and, as a shot, a body.

## Possession

The ball is free or controlled by exactly one player. Possession is part of the
ball state in `sim-match` (`matchState.hpp`); the
[ball movement](#ball-movement) system keeps a controlled ball with its carrier.

### State

`BallState::owner` is the id of the player in control, empty while the ball is
free; `isControlled()` tells the two apart.

- **At most one owner.** Possession is one optional id on the ball, not a flag
  per player. Two players can never own the ball, and player and ball can never
  disagree about who has it.
- **Only players in the state.** `MatchState::create()` rejects an owner no
  player in the state has (`kUnknownBallOwner`);
  `MatchStateWriter::setBallOwner()` throws `std::invalid_argument` for one.

### A controlled ball follows its carrier

A controlled ball at its carrier's feet sits `carryDistance` (0.5 m) ahead of
him along his facing and moves with his velocity:

```text
ball.position = carrier.position + carrier.facing · carryDistance, moved onto the pitch
ball.velocity = carrier.velocity
ball.height = 0, ball.verticalVelocity = 0, ball.spin = 0
```

A controlled ball is always on the ground, whatever it did before: a player
keeps the ball at his feet. A carrier on a line does not carry it out of play: a
carried ball that would lie off the pitch moves to the nearest point on it.

The ball movement system applies this every tick, to the carrier as the
movement system leaves him after the same tick. It moves and turns him with the
same pure functions, `stepPlayerMovement()` and `facingAfterMove()`, so ball
and carrier end every step together. `carriedBallPosition(carrier, physics)`
computes the position.

So the two systems are a pair: the prediction holds only if player movement
runs in the same tick. `makeMatchSystems()` installs both, every tick. A custom
system list without the movement system, or at another interval, may use the
ball system only for free balls. (Systems cannot see each other's writes, so
the ball cannot simply follow the carrier's new position.)

**Touched on.** A carrier who turns, carries or dribbles plays the ball ahead
of him with a touch ([ball carrying](on-ball.md#ball-carrying)). It stays his —
`owner` does not change — but rolls on its own until he or an opponent gets to
it. `BallState::touch` records the touch's tick and how many seconds later he
may take the ball again. `isAtFeet()` tells a ball at his feet from one touched
on. Only an owned ball at the feet can be touched on, never one in the hands;
giving the ball to anyone ends the touch.

The ball's velocity is the carrier's in every tick it is at his feet, so no
earlier velocity survives a change of possession: a player who takes a rolling
ball stands with it at his feet, at his own speed. Whoever releases the ball —
with a pass, for example — sets its new velocity.

### Changing possession

| Change                  | How                                                          |
|-------------------------|--------------------------------------------------------------|
| a scenario or test gives the ball to a player | `GiveBallCommand{playerId}`                    |
| a player receives, intercepts or recovers it  | the first to reach a free ball takes it; see [reception](#reception-interceptions-and-loose-balls) |
| a player passes         | pass execution releases it with the pass velocity; see [passing](on-ball.md#passing) |
| an opponent gets to a touched ball | he wins it; see [ball carrying](on-ball.md#ball-carrying) |

A `GiveBallCommand` applies at the start of its tick; the ball is at the new
owner's feet at the end of that tick. Possession changes are state changes, so
replays reproduce them and the state hash covers the owner.

### Configuration

`BallPhysics::carryDistance` (0.5 m by default) is part of `MatchConfig` and of
every replay. It must be finite and not negative.

### Last touch

`BallState::lastTouch` records who last played the ball, and when: the passer
when a pass is kicked, the carrier when he touches the ball on, the new owner
when a `GiveBallCommand` gives him the ball. It is empty until someone touches
the ball and, like the owner, can only name a player in the state
(`kUnknownLastTouch`).

### What this is not

A carrier loses a ball at his feet only in a [duel](duels.md#duels-on-the-ball),
and a touched-on ball only to an opponent who reaches it first
([ball carrying](on-ball.md#ball-carrying)).

## Reception, interceptions and loose balls

A free ball belongs to whoever reaches it first. Two parts of `sim-match` decide
who: **reception** (`reception.hpp`) resolves every tick which player gains
control of a free ball on its way, and **ball pursuit** (`pursuit.hpp`) decides
who goes after it.

### Gaining control

The [ball movement](#ball-movement) system moves a free ball from its start to
its end position in a tick — rolling, flying or bouncing. In the same tick every
player moves as the movement system moves him. `findBallClaim()` checks every
player against the ball:

- **Reach.** Player and ball move in straight lines over the tick. A player
  gains the ball if they come within `controlRadius` (1 m) at any moment of the
  tick, so a fast pass cannot slip through a player between ticks.
  `findContact()` computes the first such moment.
- **Leaving him.** A ball that starts the tick within his reach and moves away
  from him — relative to him, so one he runs after faster than it goes still
  comes to him — is no contact: kicked on from beside him, it is gone. The test
  is along the ground: a ball in the air that passed over his reach and drops
  only behind him is gone too. Before,
  a presser standing a metre from the carrier took every pass and touch off his
  foot the moment it was played, even standing behind him; half the passes
  under close pressure went that way.
- **Reach upwards.** A ball higher than `controlHeight` (1 m) at the moment of
  contact passes over the player. The height comes from the flight at that
  moment (`ballHeightAfter()`), not from the ends of the tick, which miss a
  bounce in between and put a ball that left the pitch flat on the line. So
  `findBallClaim()` takes the whole step from `stepFreeBallTimed()`. A ball
  above `controlHeight` that a player reaches first is contested in the air
  ([aerial duels](duels.md#aerial-duels-and-headers)).
- **At full stretch.** An outfield opponent of the passer who gets to a pass
  only farther than `cleanRadius` (0.7 m) from the ball's way — the last 30 cm
  of his reach — while it is still at least `deflectSpeed` (8 m/s) fast does not
  control it: it comes off him as a shot does off a body (`deflectShot()`, one
  more `kExecution` draw), and is loose with him as its last touch.
- **No instant reclaim.** The ball's last touch cannot take it back for
  `reclaimDelaySeconds` (0.3 s), so a pass does not stick to the passer's foot.
  Nobody may stop a goalkeeper letting the ball go from his hands (Law 12): his
  opponents cannot play it that long either
  ([goalkeeper](goalkeeper.md#positioning-and-sweeping)).
- **On his feet.** A player off his feet after a
  [duel](duels.md#off-his-feet) takes no ball, also in the step he stumbled in.
- **Not a fast shot.** A [shot](on-ball.md#shooting) that is still fast comes off
  an outfield player in its way instead of being taken; `findBallContact()`
  looks for that body with its own reach. The keeper of the goal a shot attacks
  does not compete for it: he meets it with his dive where it passes him. While
  busy with a dive, or down after it, he takes no ball
  ([shot stopping](goalkeeper.md#shot-stopping)).
- **The keeper's hands.** A goalkeeper in his own penalty area, with the ball in
  it, at the start of the tick reaches it with his hands: within `handsRadius`
  (1.2 m) and up to `handsHeight` (2.2 m) instead of the feet's `controlRadius`
  and `controlHeight`. A ball above `controlHeight` he goes up for, and gets it
  with his hands where he and the ball are in his area when he meets it, in the
  air ([aerial duels](duels.md#aerial-duels-and-headers)). Outside his area he
  has his feet like everyone else
  ([goalkeeper](goalkeeper.md#positioning-and-sweeping)).
- **Competing claims.** The earliest contact in the tick wins. At the same
  moment the player who comes closer wins; at the same distance the lower player
  id. This is deterministic whatever the order of players in the state.

The winner owns the ball from the end of the tick, at his feet
([possession](#possession)), and becomes its last touch.

The same rule covers every way of getting the ball:

| Situation     | What it looks like                                                     |
|---------------|------------------------------------------------------------------------|
| reception     | a teammate of the passer reaches the pass                              |
| interception  | an opponent of the passer reaches the pass on its way                  |
| missed pass   | nobody reaches it; it rolls on until it stops or leaves the pitch      |
| loose ball    | a ball at rest, or rolling slowly, is recovered by the first to reach it |

Match events tell these apart; control itself does not care who kicked the
ball.

A ball its owner has touched on ([ball carrying](on-ball.md#ball-carrying)) is
won the same way, but is still his. He takes it back once the touch's seconds
have passed, without the reclaim delay; his teammates leave it to him; an
opponent who reaches it first wins it. Pursuit sends only opponents who get to
it before him, at his running speed with the ball: a defender steps in to a
touch too heavy for its owner and leaves one the owner keeps. A goalkeeper
judges it against the owner.

### Going after the ball

While the ball is free, the ball pursuit system sends one player per side after
it, ten times a second:

1. **Interception point.** For each player, `findInterception()` predicts the
   ball's path — rolled with the exact free-ball step in `sampleSeconds`
   (0.1 s) steps, up to `horizonSeconds` (8 s) — and finds the earliest point he
   reaches no later than the ball (`estimateArrivalSeconds()`, see
   [spatial queries](players.md#spatial-queries)). If he reaches none before
   the ball stops, his interception is where it stops, when he gets there.
2. **Chaser.** Per side, the player with the earliest interception chases it;
   ties go to the lower id. He becomes his side's chaser,
   `MatchState::chaser(side)`, and his movement target is the interception
   point, overriding any target from a command. Everyone else keeps his target.
3. **The passer does not chase his own pass** while the ball moves.
   **A goalkeeper** who would chase a ball the opponent played last first
   decides whether he comes: his head start on the opponents' earliest player,
   misjudged as his anticipation allows, against his tactic's sweeping dial. If
   he stays, his side's next earliest player chases it
   ([goalkeeper](goalkeeper.md#positioning-and-sweeping)). A keeper busy with a
   dive chases nothing ([shot stopping](goalkeeper.md#shot-stopping)), and
   neither does a player off his feet after a [duel](duels.md#off-his-feet).
4. **Handing over.** A chaser's target belongs to pursuit. A player who stops
   being the chaser — a teammate reaches the ball earlier, or anyone controls
   it — has his target cleared and stops where he is, instead of running on to
   where the ball was going. A receiver who has the ball does not keep running
   to his interception point.

Pursuit reads the ball directly, not through
[perception](players.md#perception) — a simplification until off-ball movement
uses perceived positions. A player who chased the ball stays where he ran once
someone controls it; for a side with a tactic, the
[desired region](team-tactics.md#desired-region) gives his next target.

### Configuration

Both are part of `MatchConfig` and of every replay:

| Field                           | Default | Meaning                                         |
|---------------------------------|---------|-------------------------------------------------|
| `reception.controlRadius`       | 1 m     | how close a free ball must come to be controlled |
| `reception.cleanRadius`, `reception.deflectSpeed` | 0.7 m, 8 m/s | an opponent of the passer farther than this from a pass this fast only deflects it |
| `reception.reclaimDelaySeconds` | 0.3 s   | how long the last touch cannot take the ball back |
| `reception.controlHeight`       | 1 m     | how high the ball may be and still be taken at the feet |
| `pursuit.intervalTicks`         | 3       | ticks between chase decisions                   |
| `pursuit.sampleSeconds`         | 0.1 s   | resolution of the predicted ball path           |
| `pursuit.horizonSeconds`        | 8 s     | how far ahead the path is predicted             |

`horizonSeconds / sampleSeconds` may be at most 1000 (`kMaxPursuitSamples`).
Every player searches the whole path, so a finer or longer prediction would slow
each pursuit update without limit. A replay file that asks for more is rejected.

### What this is not

No first-touch quality, no deflection, no failed control: a player who reaches
the ball at a height he can play has it. A carrier's touches on are
[ball carrying](on-ball.md#the-touchs-error). Heading a ball and jumping for one
are [aerial duels](duels.md#aerial-duels-and-headers); fighting for one at a
carrier's feet are [duels](duels.md#duels-on-the-ball).
