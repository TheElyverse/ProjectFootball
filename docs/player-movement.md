# Player movement

Players move toward targets assigned by commands. The movement model lives in
`sim-match` (`playerMovement.hpp`) and plugs into the [match loop](match-loop.md)
as the system `makePlayerMovementSystem()`, which runs every tick and writes
player positions, velocities and facings.

## Targets

A `MovePlayerCommand` sets a player's `target` (see [match loop](match-loop.md),
section *Commands*). A new target replaces the previous one; the target stays
after the player has reached it, and he stays there.

**Targets are kept on the pitch.** A target off the pitch is replaced by the
nearest point on it, `Pitch::clamp()`: a target behind the goal line moves onto
the goal line, one beyond a corner onto the corner. This is the documented
boundary rule for movement. A player can still leave the pitch briefly when his
momentum carries him past the touchline after a sudden change of target; being
off the pitch is a valid state, and what it means is for a rules system to
decide.

Without a target a player slows down and stands still where he stops.

## Limits

Every player has two `PlayerAttributes`, fixed for the match:

| Attribute      | Default | Meaning                                                   |
|----------------|---------|-----------------------------------------------------------|
| `maxSpeed`     | 7.5 m/s | the speed a player never exceeds                          |
| `acceleration` | 4 m/s²  | the largest change of velocity per second, in any direction |

One acceleration limits speeding up, slowing down and turning alike. The values
describe the predefined test players of the sandbox; generated players will
derive theirs from capabilities.

## One tick of movement

`stepPlayerMovement(player, secondsPerTick)` is a pure function of the player and
the tick length, so the ball following its carrier and arrival-time estimates can
predict movement with exactly the same rule:

1. **Desired velocity.** Toward the target, at `maxSpeed` or at the braking speed,
   whichever is lower. The braking speed is the fastest speed from which the
   player can still stop on the target, braking at full acceleration from the
   next tick on:
   `v = (-a·Δt + sqrt((a·Δt)² + 8·a·d)) / 2` for the distance `d`. Without a
   target, or standing on it, the desired velocity is zero.
2. **Velocity.** The current velocity moves toward the desired one by at most
   `acceleration · Δt`. Both lie within `maxSpeed`, so the new velocity does too.
3. **Position.** The player moves by `velocity · Δt`.
4. **Arrival.** If that step would reach or pass the target and the player is
   slow enough to stop there — at most `2 · acceleration · Δt` — he ends on the
   target, at rest. Braking guarantees this on a straight approach, so a player
   never runs past his target. A player carried past a new target by his
   momentum runs on and comes back instead of stopping dead.

The continuous braking rule `v = sqrt(2·a·d)` looks simpler but ignores that a
tick moves at the new speed for its whole length: a player following it arrives
at over 1 m/s and stops abruptly. The discrete rule arrives at about one tick's
worth of acceleration.

Lengths use `std::sqrt` of the squared length rather than `std::hypot`:
`sqrt` is correctly rounded by IEEE 754, so the result does not depend on the
platform's math library.

## Facing

The movement system also turns players, with `facingAfterMove()`:

- a player moving faster than `kFacingRunSpeed` (1 m/s) looks where he runs,
- a slower or standing player looks at the ball,
- a player standing exactly on the ball keeps his facing.

He turns at once. A turning rate, and looking elsewhere than where he runs,
belong to later body-orientation work. Facing decides what a player can see; see
[perception](perception.md).

## Example

From rest, a player covers 20 m in about 4.5 s at the default limits: 1.9 s to
reach 7.5 m/s over 7 m, 0.8 s at full speed, and 1.9 s braking over the last 7 m.

## What this is not

There is no reaction time, no fatigue, no turning rate, and no collision
between players: two players can stand on the same spot. Off-ball positioning
— where a player should go — is for later tactical systems; this model only
decides how he gets there.
