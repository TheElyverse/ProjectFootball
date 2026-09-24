# Passing

A pass is split into what the player decides and how well he executes it, as
everywhere in the simulation (implementation plan, section 6.6). The decision is
a `PassIntent`; execution turns it into a ball velocity with a random error. The
rules live in `sim-match` (`passing.hpp`), and the [ball movement](ball-movement.md)
system executes passes.

## The intent

`PassIntent` is the *what*: the passer, the target position, the speed the ball
should leave the foot with, and optionally the intended receiver. It sits in
`MatchState::pendingPass()` until the ball system plays it; every state created
from a spec starts without one.

Two things create an intent:

- the decision system of a player on the ball (coming with decisions), and
- a `PassCommand{playerId, target, speed, receiver}`, so a scenario or test can
  request one specific pass and watch it being executed. The command is
  validated when scheduled: the passer and receiver must exist, the target must
  be finite and the speed positive and finite.

`planPassSpeed(distance, physics, config)` is the speed a pass over a distance
needs to reach its target still rolling at `arrivalSpeed` (4 m/s):
`sqrt(arrivalSpeed² + 2 · rollingDeceleration · distance)`, capped at `maxSpeed`
(22 m/s). `passReach(speed, physics)` is how far a pass at that speed rolls before
it stops, `speed² / (2 · rollingDeceleration)`. A pass that even `maxSpeed` cannot
carry to its target stops short.

## Execution

In the next step it runs, the ball system plays the pending pass:

1. **Only the owner passes.** If the passer does not own the ball — someone else
   has it, or it is free — the intent is discarded and nothing happens.
2. **Release.** The ball becomes free, and the passer becomes its `lastTouch`
   with the current tick.
3. **Kick.** `executePass()` gives the ball its velocity: from where the ball is
   toward the target, at the intended speed, off by a random execution error:
   - direction: up to `directionError` (0.03) meters off line per meter along
     it, uniformly distributed — about ±1.7°,
   - speed: up to `speedError` (5 %) harder or softer, uniformly distributed,
   - never faster than `maxSpeed`.

   A target on the ball itself is played along the passer's facing.
4. **Roll.** From then on the ball is an ordinary free ball: ground friction and
   the pitch boundary decide its trajectory and reach.

Either way the pending pass is cleared, so an intent is played at most once.

A `PassCommand` for tick `t` is applied at the start of that step and executed
in the same step. An intent the decision system writes during tick `t` is
executed in the step of tick `t + 1`.

The execution error draws two numbers from the simulation's `kExecution` random
stream per executed pass, always two, so the same seed reproduces every pass. A
test can set both errors to zero to predict a pass exactly.

## Configuration

`PassConfig` is part of `MatchConfig` and of every replay:

| Field            | Default  | Meaning                                              |
|------------------|----------|------------------------------------------------------|
| `arrivalSpeed`   | 4 m/s    | the speed a planned pass keeps at its target         |
| `maxSpeed`       | 22 m/s   | the hardest ground pass                              |
| `directionError` | 0.03     | largest sideways deviation per meter along the line  |
| `speedError`     | 0.05     | largest relative deviation of the speed              |

The ball system rejects non-positive speeds, negative errors, a speed error of
1 or more, and any non-finite value.

## What this is not

All passes are ground passes. There is no chip, lofted pass or shot, and the
error does not yet depend on the passer's technique, pressure or body shape — the
capabilities that will shape it arrive with the player model. Receiving the ball
comes with receptions and interceptions.
