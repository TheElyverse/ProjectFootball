# Ball

The ball on its own and between players: how it rolls, flies and bounces, who
owns it, and how a player gains control of a free ball.

- [Ball movement](#ball-movement)
- [Possession](#possession)
- [Reception, interceptions and loose balls](#reception-interceptions-and-loose-balls)

## Ball movement

A free ball rolls on the pitch, flies through the air and bounces on the grass.
The physics live in `sim-match` (`ballPhysics.hpp`) as pure functions; the
system `makeBallMovementSystem(BallMovementRules)` (`ballMovement.hpp`) plugs them
into the [match loop](match-engine.md#match-loop), runs every tick and writes the ball's
position, velocity, height, vertical velocity and spin. A free ball knows
nothing about players: it moves the same with or without them. A controlled
ball follows its carrier and stays on the ground; see
[possession](#possession). The same system plays pending passes, which turn a
controlled ball into a free one (see [passing](on-ball.md#passing)), and hands a free
ball to the first player who can reach it (see [reception](#reception-interceptions-and-loose-balls)).

### The ball's state

`BallState` carries the pitch plane and the third dimension:

| Field              | Unit  | Meaning                                              |
|--------------------|-------|------------------------------------------------------|
| `position`         | m     | where the ball is on the pitch                       |
| `velocity`         | m/s   | how it moves in the pitch plane                      |
| `height`           | m     | how far above the grass it is                        |
| `verticalVelocity` | m/s   | how fast it rises; negative while it falls           |
| `spin`             | rad/s | topspin, or backspin when negative                   |

A ball with no height and no vertical velocity lies or rolls on the grass, and
`isAtRest()` holds when it does not move at all. The distinction matters: a ball
at the apex of its flight has no velocity in the pitch plane either, and it is
very much in play. `isInFlight(ball)` is the other half of the pair, and it is
true for a ball that has just been lifted off the turf, before it has gained any
height.

Spin is one signed number rather than a vector: top- and backspin about the
horizontal axis across the ball's direction of travel. Sidespin, and with it a
curving flight, is not modelled -- see [what this is not](#what-this-is-not).

### Constants

`BallPhysics` holds them all, and `validate()` rejects a set a ball cannot have.
Every one of them is part of `MatchConfig` and therefore of every
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

The rolling deceleration and gravity must be positive and finite, the carry
distance, the drag, the spin decay and the resting speed finite and not
negative, and the three shares between 0 and 1.

### Rolling

The ball decelerates at a constant rate along its direction of travel. A ball
rolling at speed `v` stops after `v / a` seconds and `v² / (2·a)` meters: at the
default, a 10 m/s pass rolls about 33 m. `rollingDistance(speed, physics)`
returns that distance. Friction only ever takes speed away: the direction of a
rolling ball never changes, and it stops rather than rolling back.

The integration is the exact solution for constant deceleration, not an
approximation: the ball stops at the same point at 30 Hz, 60 Hz or any other
tick rate, and a pass planner can compute where a ball will stop without
simulating it. Rolling is unchanged from the model that came before flight;
a ball on the grass behaves exactly as it always has, down to the last bit.

### Flight

A ball off the ground, or one with a vertical velocity, flies. Gravity pulls it
down and the air takes speed from every component:

```
v(t) = v₀ · e^(-k·t)                          in the pitch plane
w(t) = (w₀ + g/k) · e^(-k·t) - g/k            upwards
```

for a drag `k` and gravity `g`, with the height the integral of `w`. Spin dies
away the same exponential way. The solution is exact for any span, so the same
flight comes out the same however the ticks are cut, and it uses
`SimCore::stableExp()` rather than `std::exp`, which makes it bit-identical on
every platform -- the same reason perception computes its own cosine (see
[match geometry](match-engine.md#match-geometry)).

**Why a linear drag.** A real football feels a drag that grows with the square
of its speed. That model has no closed form: it needs a numerical integrator,
whose result depends on the step size and on the platform's rounding -- the two
things a [replay](tooling.md#replay-format) cannot tolerate. A linear drag is solvable,
and its constant is tuned rather than measured: at the default, the terminal
speed of a falling ball is `gravity / airDrag ≈ 30 m/s`, which is about right
for a football.

**Why the terminal speed does not appear in the code.** `g/k` above is that
terminal speed, and it is a trap: every drag down to zero is a valid
configuration, and the smallest of them make `g/k` overflow and `1 - e^(-k·t)`
cancel to nothing, so a formula written in those terms turns a legal
configuration into a ball that flies to 270 m or to `NaN`. The step is
therefore written over the span `t · φ₁(k·t)` a unit speed covers and the drop
`g · t² · φ₂(k·t)` gravity adds, where

```
φ₁(x) = (1 - e^(-x)) / x        φ₂(x) = (e^(-x) - 1 + x) / x²
```

Neither ever divides by the drag. Both are summed from their Taylor series
below `x = 0.5`, where the closed forms are all cancellation and the series has
long converged, and `φ₁(0) = 1`, `φ₂(0) = ½` are exactly the plain parabola --
so no drag, a vanishing drag and a real one are one formula rather than a
special case each.

### Bounce

Where the flight reaches the ground within a tick, the ball bounces:

- it keeps `bounceRestitution` of its vertical speed, upwards;
- it keeps `bounceGrip` of its speed along the ground;
- it spends `spinTransfer` of its spin driving itself on -- topspin forward,
  backspin against the travel, never far enough to turn the ball around. The
  spin it spends is gone. A ball dropping straight down has no direction to
  drive along and keeps none.
- a bounce that leaves it rising no faster than `restingVerticalSpeed` leaves
  it down: the ball stays on the grass and rolls the rest of the tick, rather
  than hopping ever smaller hops forever. No faster rather than slower, so a
  ball that a `bounceRestitution` of 0 leaves at a standstill settles on that
  one impact even where `restingVerticalSpeed` is 0 too.

The tick is split at the moment the ball lands rather than at its end. That is
what makes a flight independent of the tick rate: a bounce happens at its own
time, not at the next multiple of 1/30 s. The tolerance is rounding, nothing
more -- two seconds of flight and a bounce stepped at 30 Hz, 120 Hz and 300 Hz
agree to within a micrometer, which is what the tests pin. The landing time has no closed form
under drag, so it is found by bisection over the flight -- a fixed 64 halvings,
which reach the last bits of a double and cost the same every time. At most
eight bounces are resolved within one tick; a ball still hopping after that hops
no higher than rounding and is left down.

### Predicting a landing

`predictBallLanding(ball, physics)` answers where and when a flying ball comes
down and how high it gets on the way, without stepping the match: the same
bisection over the closed-form flight, bracketed by doubling a one-second
horizon. It is empty for a ball already on the ground, and for a ball without
gravity, which never comes down. Chasers use it through
[reception](#reception-interceptions-and-loose-balls), the goalkeeper among them when he decides whether
to come for a ball ([goalkeeper](goalkeeper.md#positioning-and-sweeping)).

The pitch boundary is not applied: this is the flight's own landing, and what
happens to a ball that leaves the pitch is for the systems to decide.

### Pitch boundary

A ball that crosses a touchline or goal line stops on the line at the crossing
point, on the ground, at rest and without spin, in the air as on the grass: the
ball is out of play and waits there. This is the documented stand-in until a
rules system decides on throw-ins, goal kicks and corners; the opt-in
[restart](match-engine.md#restarts) system gives such a ball to the side due to restart. A
ball that crossed a goal line between the posts and under the crossbar is a
goal, and before it gets there the posts and the crossbar can turn it away
([shooting](on-ball.md#shooting)). A
ball rolling along a line, or resting on it, is on the pitch
(`Pitch::contains()` includes the edges) and keeps rolling.

A free ball always travels along a straight line in the pitch plane -- drag acts
along its direction of travel and a bounce only scales its speed -- so one
crossing point describes a whole tick, however often the ball bounces within it.

Such a ball travels only part of the tick, and `stepFreeBallTimed()` reports
that part alongside the ball. [Reception](#reception-interceptions-and-loose-balls) needs it: a contact
somewhere along the shortened path happened earlier in the tick than the same
fraction of a whole one, and a ball put down flat on the line says nothing
about how high it was when it crossed. The part is taken as the fraction of the
way to the line, which is the fraction of the time to within whatever the ball
gains or loses in speed over a thirtieth of a second.

A ball that already lies off the pitch — only possible in a hand-built state —
plays on without this rule.

### Starting with a rolling ball

`makeSevenASideKickoff(pitch, ballVelocity)` places the ball on the center spot
with the given velocity, at rest by default. A non-finite velocity is rejected
like any other invalid state.

### What this is not

Only a [shot](on-ball.md#shooting) and a goalkeeper's long ball
([goalkeeper distribution](goalkeeper.md#distribution)) lift the ball off the
ground: every other pass is a ground pass, and it leaves the foot without
height, vertical velocity or spin. Crosses and outfield lofted passes are what
else will fill the third dimension. Who
wins a high ball is decided in the air ([aerial duels](duels.md#aerial-duels-and-headers)).

The flight has no sidespin and so no curve: a Magnus force needs a sine and a
cosine that are bit-identical on every platform, and `sim-core` has no such
trigonometry yet. Players perceive the ball's position on the pitch, not its
height. There is no air resistance while the ball rolls, no collision between
ball and player but a shot coming off a body, nothing else for it to hit but
the grass and the goal frame, and no wind.

## Possession

The ball is either free or controlled by exactly one player. Possession lives in
the ball state of `sim-match` (`matchState.hpp`); the
[ball movement](#ball-movement) system keeps a controlled ball with its carrier.

### State

`BallState::owner` is the id of the player in control of the ball, empty while
the ball is free; `isControlled()` tells the two apart.

- **At most one owner.** Possession is one optional id on the ball, not a flag
  per player, so two players can never own the ball at the same time and a
  player and the ball can never disagree about who has it.
- **Only players in the state.** `MatchState::create()` rejects an owner no player
  in the state has (`kUnknownBallOwner`), and `MatchStateWriter::setBallOwner()`
  throws `std::invalid_argument` for one.

### A controlled ball follows its carrier

A controlled ball at its carrier's feet sits `carryDistance` (0.5 m) ahead of
him along his facing and moves with his velocity:

```text
ball.position = carrier.position + carrier.facing · carryDistance, moved onto the pitch
ball.velocity = carrier.velocity
ball.height = 0, ball.verticalVelocity = 0, ball.spin = 0
```

A controlled ball is always on the ground, whatever it did before someone took
it: a player keeps the ball at his feet.

A carrier standing on a line does not carry the ball out of play: a carried ball
that would lie off the pitch is moved onto its nearest point.

The ball movement system applies this rule every tick, to the carrier as the
movement system leaves him after the same tick: it moves and turns him with the
same pure functions, `stepPlayerMovement()` and `facingAfterMove()`, so ball and
carrier end every step together. `carriedBallPosition(carrier, physics)` computes
the position.

This makes the two systems a pair: the ball system predicts the carrier's move,
so the prediction only comes true if the player movement system runs in the same
tick. `makeMatchSystems()` always installs both, every tick. A custom system
list that runs the ball system without the movement system, or at another
interval, may only use it for free balls; with a controlled ball it would leave
the ball where the carrier would have gone. Systems read the state from before
the step and cannot see what another system writes, so the ball cannot simply
follow the carrier's committed position instead.

**Touched on.** A carrier who turns, carries or dribbles plays the ball ahead
of him with a touch ([ball carrying](on-ball.md#ball-carrying)): it stays his --
`owner` does not change -- but rolls on its own until he or an opponent gets to
it, and `BallState::touch` records the touch's tick and how many seconds after
it he may take the ball again. `isAtFeet()` tells a ball at his feet from one
touched on. Only an owned ball at the feet can be touched on, never one held in
the hands, and giving the ball to anyone ends the touch.

Because the ball's velocity is the carrier's in every tick it is at his feet, no
velocity from before survives a change of possession: a player who takes a rolling
ball stands with it at his feet, at his own speed. When a ball is released — by a
pass, for example — whoever releases it sets its new velocity.

### Changing possession

| Change                  | How                                                          |
|-------------------------|--------------------------------------------------------------|
| a scenario or test gives the ball to a player | `GiveBallCommand{playerId}`                    |
| a player receives, intercepts or recovers it  | the first to reach a free ball takes it; see [reception](#reception-interceptions-and-loose-balls) |
| a player passes         | pass execution releases it with the pass velocity; see [passing](on-ball.md#passing) |
| an opponent gets to a touched ball | he wins it; see [ball carrying](on-ball.md#ball-carrying) |

A `GiveBallCommand` applies at the start of its tick; the ball is at the new
owner's feet at the end of that tick. Possession changes are state changes like
any other, so replays reproduce them and the state hash covers the owner.

### Configuration

`BallPhysics::carryDistance` (0.5 m by default) is part of `MatchConfig` and of
every replay. It must be finite and not negative.

### Last touch

`BallState::lastTouch` records who last played the ball and in which tick: the
passer when a pass is kicked, the carrier when he touches the ball on, the new
owner when a `GiveBallCommand` hands him the ball. It is empty until someone
has touched the ball, and like the owner it can only name a player in the state
(`kUnknownLastTouch`).

### What this is not

A carrier loses a ball at his feet only in a [duel](duels.md#duels-on-the-ball), and one he
touched on to an opponent who gets to it first ([ball carrying](on-ball.md#ball-carrying)).

## Reception, interceptions and loose balls

A free ball belongs to whoever reaches it first. Two parts of `sim-match` decide
who that is: **reception** (`reception.hpp`) resolves, every tick, which player
gains control of a free ball on its way, and **ball pursuit** (`pursuit.hpp`)
decides who goes after it.

### Gaining control

The [ball movement](#ball-movement) system moves a free ball from where it is
at the start of a tick to where it is at the end -- rolling, flying or
bouncing. During the same tick every
player moves as the movement system moves him. `findBallClaim()` checks every
player against the ball:

- **Reach.** Player and ball both move in a straight line over the tick. A
  player gains the ball if they come within `controlRadius` (1 m) of each other
  at any moment of the tick — so a fast pass cannot slip through a player
  between two ticks. `findContact()` computes the first such moment.
- **Reach upwards.** A ball higher than `controlHeight` (1 m) at the moment of
  contact passes over the player: he cannot take it at his feet. The height is
  asked of the flight itself at that moment (`ballHeightAfter()`), not read
  between the two ends of the tick: a ball that bounces within the tick is at
  neither end's height in between, and one that leaves the pitch ends the tick
  lying flat on the line however high it crossed it -- a two-meter ball on its
  way out would otherwise be handed to whoever stands on the line. The moment
  itself is a fraction of the path the ball really travelled, which for a ball
  the line stops is only part of the tick, so `findBallClaim()` takes the step
  `stepFreeBallTimed()` makes rather than only the state it ends in.
  A ball above `controlHeight` that a player gets to first is contested in
  the air instead ([aerial duels](duels.md#aerial-duels-and-headers)).
- **No instant reclaim.** The ball's last touch cannot take it back for
  `reclaimDelaySeconds` (0.3 s) after touching it, so a pass does not stick to
  the passer's foot.
- **On his feet.** A player off his feet after a [duel](duels.md#off-his-feet)
  takes no ball, also in the step he stumbled in.
- **Not a fast shot.** A [shot](on-ball.md#shooting) still fast comes off an outfield
  player in its way instead of being taken by him; `findBallContact()` looks
  for that body with its own reach. The keeper of the goal a shot attacks does
  not compete for it at all: he meets it with his dive where it passes him,
  and while he is busy with a dive or down after it he takes no ball
  ([shot stopping](goalkeeper.md#shot-stopping)).
- **The keeper's hands.** A goalkeeper who stands in his own penalty area, with
  the ball in it, at the start of the tick reaches it with his hands: within
  `handsRadius` (1.2 m) and up to `handsHeight` (2.2 m) instead of the feet's
  `controlRadius` and `controlHeight`; a ball above `controlHeight` he goes
  up for, and gets to with his hands where he and it are in his area when he
  meets it, in the air ([aerial duels](duels.md#aerial-duels-and-headers)). Outside his area he has his
  feet like everyone else ([goalkeeper](goalkeeper.md#positioning-and-sweeping)).
- **Competing claims.** The earliest contact in the tick wins. At the same
  moment the player who comes closer wins, and at the same distance the lower
  player id: deterministic whatever the order of players in the state.

The winner owns the ball from the end of the tick, with the ball at his feet
(see [possession](#possession)), and becomes its last touch.

The same rule covers every way of getting the ball:

| Situation     | What it looks like                                                     |
|---------------|------------------------------------------------------------------------|
| reception     | a teammate of the passer reaches the pass                              |
| interception  | an opponent of the passer reaches the pass on its way                  |
| missed pass   | nobody reaches it; it rolls on until it stops or leaves the pitch      |
| loose ball    | a ball at rest, or rolling slowly, is recovered by the first to reach it |

Telling these apart is for match events; control itself does not care who
kicked the ball.

A ball its owner has touched on ([ball carrying](on-ball.md#ball-carrying)) is won the
same way, but is still his: he takes it back once the touch's seconds have
passed, without the reclaim delay, his teammates leave it to him, and an
opponent who reaches it first wins it from him. Pursuit sends only those of
the other side after it who get to it before him, at his running speed with
the ball: a defender steps in to a touch too heavy for its owner, and leaves
one the owner keeps to him. A goalkeeper judges it against the owner.

### Going after the ball

While the ball is free, the ball pursuit system sends one player per side after
it, ten times a second:

1. **Interception point.** For each player, `findInterception()` predicts the
   ball's path — rolled with the exact free-ball step in `sampleSeconds` (0.1 s)
   steps, up to `horizonSeconds` (8 s) — and finds the earliest point he can reach
   no later than the ball (`estimateArrivalSeconds()`, see
   [spatial queries](players.md#spatial-queries)). If he reaches none before the ball
   stops, his interception is where it stops, when he gets there.
2. **Chaser.** Per side, the player with the earliest interception chases it; ties
   go to the lower id. He becomes his side's chaser, `MatchState::chaser(side)`,
   and his movement target the interception point, overriding any target a
   command gave him. Everyone else keeps his target.
3. **The passer does not chase his own pass** while the ball still moves.
   **A goalkeeper** who would chase a ball the opponent played last first
   decides whether he comes: his head start on the opponents' earliest player,
   misjudged as his anticipation allows, against his tactic's sweeping dial. If
   he stays, his side's next earliest player chases it
   ([goalkeeper](goalkeeper.md#positioning-and-sweeping)). A keeper busy with a dive chases nothing
   ([shot stopping](goalkeeper.md#shot-stopping)), nor does a player off his feet after a
   [duel](duels.md#off-his-feet).
4. **Handing over.** A chaser's target belongs to pursuit. A player who stops
   being the chaser -- a teammate reaches the ball earlier, or anyone controls
   it -- has his target cleared and stops where he is, instead of running on to
   where the ball was going to be. A receiver who has the ball does not keep
   running to his interception point.

Pursuit reads the ball directly rather than through
[perception](players.md#perception): a simplification until off-ball movement uses
perceived positions. A player who has chased the ball stays where he ran to once
someone controls it; for a side with a tactic, the
[desired region](team-tactics.md#desired-region) gives him his next target.

### Configuration

Both are part of `MatchConfig` and of every replay:

| Field                           | Default | Meaning                                         |
|---------------------------------|---------|-------------------------------------------------|
| `reception.controlRadius`       | 1 m     | how close a free ball must come to be controlled |
| `reception.reclaimDelaySeconds` | 0.3 s   | how long the last touch cannot take the ball back |
| `reception.controlHeight`       | 1 m     | how high the ball may be and still be taken at the feet |
| `pursuit.intervalTicks`         | 3       | ticks between chase decisions                   |
| `pursuit.sampleSeconds`         | 0.1 s   | resolution of the predicted ball path           |
| `pursuit.horizonSeconds`        | 8 s     | how far ahead the path is predicted             |

`horizonSeconds / sampleSeconds` may be at most 1000 (`kMaxPursuitSamples`):
every player searches the whole path, so a finer or longer prediction would
make each pursuit update slower without limit. A replay file that asks for more
is rejected.

### What this is not

There is no first-touch quality, no deflection and no failed control: a player
who reaches the ball at a height he can play has it. A carrier's touches on are
[ball carrying](on-ball.md#the-touchs-error). Heading a ball and
jumping for one are [aerial duels](duels.md#aerial-duels-and-headers), fighting for one at a
carrier's feet [duels](duels.md#duels-on-the-ball).
