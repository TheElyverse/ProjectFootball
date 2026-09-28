# Ball movement

A free ball rolls on the pitch, flies through the air and bounces on the grass.
The physics live in `sim-match` (`ballPhysics.hpp`) as pure functions; the
system `makeBallMovementSystem(BallPhysics)` (`ballMovement.hpp`) plugs them
into the [match loop](match-loop.md), runs every tick and writes the ball's
position, velocity, height, vertical velocity and spin. A free ball knows
nothing about players: it moves the same with or without them. A controlled
ball follows its carrier and stays on the ground; see
[possession](possession.md). The same system plays pending passes, which turn a
controlled ball into a free one (see [passing](passing.md)), and hands a free
ball to the first player who can reach it (see [reception](reception.md)).

## The ball's state

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

## Constants

`BallPhysics` holds them all, and `validate()` rejects a set a ball cannot have.
Every one of them is part of `MatchConfig` and therefore of every
[replay](replay-format.md).

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
| `restingVerticalSpeed` | 0.25 m/s | a bounce slower than this leaves the ball down              |

The rolling deceleration and gravity must be positive and finite, the carry
distance, the drag, the spin decay and the resting speed finite and not
negative, and the three shares between 0 and 1.

## Rolling

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

## Flight

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
[match geometry](match-geometry.md)).

**Why a linear drag.** A real football feels a drag that grows with the square
of its speed. That model has no closed form: it needs a numerical integrator,
whose result depends on the step size and on the platform's rounding -- the two
things a [replay](replay-format.md) cannot tolerate. A linear drag is solvable,
and its constant is tuned rather than measured: at the default, the terminal
speed of a falling ball is `gravity / airDrag ≈ 30 m/s`, which is about right
for a football.

## Bounce

Where the flight reaches the ground within a tick, the ball bounces:

- it keeps `bounceRestitution` of its vertical speed, upwards;
- it keeps `bounceGrip` of its speed along the ground;
- it spends `spinTransfer` of its spin driving itself on -- topspin forward,
  backspin against the travel, never far enough to turn the ball around. The
  spin it spends is gone. A ball dropping straight down has no direction to
  drive along and keeps none.
- a bounce that leaves it rising slower than `restingVerticalSpeed` leaves it
  down: the ball stays on the grass and rolls the rest of the tick, rather than
  hopping ever smaller hops forever.

The tick is split at the moment the ball lands rather than at its end. That is
what makes a flight independent of the tick rate: a bounce happens at its own
time, not at the next multiple of 1/30 s. The tolerance is rounding, nothing
more -- two seconds of flight and a bounce stepped at 30 Hz, 120 Hz and 300 Hz
agree to within a micrometer, which is what the tests pin. The landing time has no closed form
under drag, so it is found by bisection over the flight -- a fixed 64 halvings,
which reach the last bits of a double and cost the same every time. At most
eight bounces are resolved within one tick; a ball still hopping after that hops
no higher than rounding and is left down.

## Predicting a landing

`predictBallLanding(ball, physics)` answers where and when a flying ball comes
down and how high it gets on the way, without stepping the match: the same
bisection over the closed-form flight, bracketed by doubling a one-second
horizon. It is empty for a ball already on the ground, and for a ball without
gravity, which never comes down. Chasers use it through
[reception](reception.md); a goalkeeper will use it directly.

The pitch boundary is not applied: this is the flight's own landing, and what
happens to a ball that leaves the pitch is for the systems to decide.

## Pitch boundary

A ball that crosses a touchline or goal line stops on the line at the crossing
point, on the ground, at rest and without spin, in the air as on the grass: the
ball is out of play and waits there. This is the documented stand-in until a
rules system decides on throw-ins, goal kicks, corners and goals; the opt-in
[restart](restarts.md) system gives such a ball to the side due to restart. A
ball rolling along a line, or resting on it, is on the pitch
(`Pitch::contains()` includes the edges) and keeps rolling.

A free ball always travels along a straight line in the pitch plane -- drag acts
along its direction of travel and a bounce only scales its speed -- so one
crossing point describes a whole tick, however often the ball bounces within it.

A ball that already lies off the pitch — only possible in a hand-built state —
plays on without this rule.

## Starting with a rolling ball

`makeSevenASideKickoff(pitch, ballVelocity)` places the ball on the center spot
with the given velocity, at rest by default. A non-finite velocity is rejected
like any other invalid state.

## What this is not

Nothing kicks the ball off the ground yet: every pass is a ground pass, and it
leaves the foot without height, vertical velocity or spin. Shots, crosses and
lofted passes are what will fill the third dimension; headers and aerial duels
decide who wins a high ball, and until they exist a ball above
`controlHeight` simply runs through (see [reception](reception.md)).

The flight has no sidespin and so no curve: a Magnus force needs a sine and a
cosine that are bit-identical on every platform, and `sim-core` has no such
trigonometry yet. Players perceive the ball's position on the pitch, not its
height. There is no air resistance while the ball rolls, no collision between
ball and player, nothing for it to hit but the grass -- posts and crossbar wait
for the goals -- and no wind.
