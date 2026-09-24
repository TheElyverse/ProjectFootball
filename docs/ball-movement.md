# Ball movement

A free ball rolls on the pitch under ground friction. The model lives in
`sim-match` (`ballMovement.hpp`) and plugs into the [match loop](match-loop.md)
as the system `makeBallMovementSystem(BallPhysics)`, which runs every tick and
writes the ball's position and velocity. A free ball knows nothing about players:
it moves the same with or without them. A controlled ball follows its carrier;
see [possession](possession.md). The same system plays pending passes, which
turn a controlled ball into a free one (see [passing](passing.md)), and hands a
free ball to the first player to reach it (see [reception](reception.md)).

## Friction

The ball decelerates at a constant rate along its direction of travel:

| Constant              | Default   | Meaning                                    |
|-----------------------|-----------|--------------------------------------------|
| `rollingDeceleration` | 1.5 m/s²  | speed lost per second while the ball rolls |

`BallPhysics` holds it; the system rejects a value that is not positive and
finite. A ball rolling at speed `v` stops after `v / a` seconds and
`v² / (2·a)` meters: at the default, a 10 m/s pass rolls about 33 m.
`rollingDistance(speed, physics)` returns that distance.

Friction only ever takes speed away. The direction of a rolling ball never
changes, and it stops rather than rolling back.

## One tick

`stepFreeBall(ball, physics, pitch, secondsPerTick)` is a pure function:

1. A ball at rest stays at rest.
2. Otherwise its speed drops by `a·Δt`. If that would take it below zero, the
   ball stops within the tick after exactly its remaining rolling distance.
   Otherwise it moves at the average of its start and end speed for the whole
   tick.
3. If the move leaves the pitch, the ball stops on the boundary line where it
   crossed it.

Step 2 is the exact solution for constant deceleration, not an approximation:
the ball stops at the same point at 30 Hz, 60 Hz or any other tick rate, and a
pass planner can compute where a ball will stop without simulating it.

## Pitch boundary

A ball that crosses a touchline or goal line stops on the line at the crossing
point, with no velocity: the ball is out of play and waits there. This is the
documented stand-in until a rules system decides on throw-ins, goal kicks,
corners and goals. A ball rolling along a line, or resting on it, is on the
pitch (`Pitch::contains()` includes the edges) and keeps rolling.

A ball that already lies off the pitch — only possible in a hand-built state —
rolls on without this rule.

## Starting with a rolling ball

`makeSevenASideKickoff(pitch, ballVelocity)` places the ball on the center spot
with the given velocity, at rest by default. A non-finite velocity is rejected
like any other invalid state.

## What this is not

The ball has no height, spin, bounce or air resistance yet. Collisions
with players are not modeled.
