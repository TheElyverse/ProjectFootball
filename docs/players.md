# Players

How a player moves, what he perceives, and the spatial questions every
decision asks: who is near, and how soon can someone get somewhere.

- [Player movement](#player-movement)
- [Perception](#perception)
- [Spatial queries](#spatial-queries)

## Player movement

Players move toward targets that commands set. The model is in `sim-match`
(`playerMovement.hpp`). The system `makePlayerMovementSystem()` runs every tick
in the [match loop](match-engine.md#match-loop) and writes positions,
velocities and facings. Two exceptions: a goalkeeper busy with a dive goes where
his dive takes him, whatever his target
([shot stopping](goalkeeper.md#his-body)); a player off his feet after a
[duel](duels.md#off-his-feet) moves as if he had no target and slows to a stop
(`asMoving()`).

### Targets

A `MovePlayerCommand` sets a player's `target` (see
[match loop](match-engine.md#match-loop), section *Commands*). A new target
replaces the old one. The target stays after he reaches it, and so does he.

**Targets stay on the pitch.** `Pitch::clamp()` replaces a target off the pitch
with the nearest point on it: behind the goal line onto the goal line, beyond a
corner onto the corner. This is the boundary rule for movement. Momentum after
a sudden change of target can still carry a player over the touchline for a
moment. Being off the pitch is a valid state; a rules system decides what it
means.

Without a target a player slows down and stands where he stops.

### Limits

Every player has two `PlayerAttributes`, fixed for the match:

| Attribute      | Default | Meaning                                                   |
|----------------|---------|-----------------------------------------------------------|
| `maxSpeed`     | 7.5 m/s | the speed a player never exceeds                          |
| `acceleration` | 4 m/s²  | the largest change of velocity per second, in any direction |

One acceleration limits speeding up, slowing down and turning. The values
describe the sandbox's test players; generated players will derive theirs from
capabilities.

**With the ball** at his feet or rolling ahead of him after a touch, he runs at
most `runningSpeed()`: `maxSpeed` times a share between
`BallPhysics::minCarrySpeedFactor` (0.8) and `maxCarrySpeedFactor` (0.95), by
his `ballControl` ([ball carrying](on-ball.md#running-with-the-ball)). The
movement system and every prediction of his move use `stepPlayerMovement()` at
that speed.

### One tick of movement

`stepPlayerMovement(player, secondsPerTick)` is a pure function of the player
and the tick length. The ball following its carrier and arrival-time estimates
use the same rule:

1. **Desired velocity.** Toward the target, at `maxSpeed` or the braking speed,
   whichever is lower. The braking speed is the fastest speed from which he can
   still stop on the target, braking at full acceleration from the next tick:
   `v = (-a·Δt + sqrt((a·Δt)² + 8·a·d)) / 2` for the distance `d`. Without a
   target, or on it, the desired velocity is zero.
2. **Velocity.** The velocity moves toward the desired one by at most
   `acceleration · Δt`. Both are within `maxSpeed`, so the result is too.
3. **Position.** He moves by `velocity · Δt`.
4. **Arrival.** If the step reaches or passes the target and he is slow enough
   to stop there — at most `2 · acceleration · Δt` — he ends on the target, at
   rest. Braking guarantees this on a straight approach, so he never runs past
   his target. Momentum can carry him past a new target; then he runs on and
   comes back instead of stopping dead.

The continuous rule `v = sqrt(2·a·d)` ignores that a tick moves at the new
speed for its whole length; a player would arrive at over 1 m/s and stop
abruptly. The discrete rule arrives at about one tick of acceleration.

Lengths use `Vec2::length()`, built on `SimCore::stableHypot()`: only correctly
rounded IEEE 754 operations, so results do not depend on the math library.

### Facing

The movement system also turns players, with `facingAfterMove()`:

- above `kFacingRunSpeed` (1 m/s) a player looks where he runs;
- slower or standing, he looks at the ball. A goalkeeper holding it looks up
  the pitch ([distribution](goalkeeper.md#the-ball-in-his-hands)). A carrier
  looks at the ball where the systems before movement left it: in a
  [duel](duels.md#duels-on-the-ball) he rolls it round him and turns with it;
- a player standing exactly on the ball keeps his facing.

He turns at once. A turning rate, and looking elsewhere than where he runs, come
with later body-orientation work. Facing decides what he can see
([perception](#perception)).

### Example

From rest at the default limits, a player covers 20 m in about 4.5 s: 1.9 s to
reach 7.5 m/s over 7 m, 0.8 s at full speed, 1.9 s braking over the last 7 m.

### What this is not

No reaction time, no fatigue, no turning rate, no collisions: two players can
stand on the same spot. Where a player should go is for the tactical systems;
this model only decides how he gets there.

## Perception

Players do not know the true match state. Each player remembers what he has
seen — the ball and other players — and every decision reads that memory, never
the state behind it. Perception is in `sim-match` (`perception.hpp`,
`observation.hpp`); the [match loop](match-engine.md#match-loop) runs it as
`makePerceptionSystem(PerceptionConfig)`.

### What a player remembers

`MatchState::perception(playerIndex)` is a `PlayerPerception`: one
`Observation` per remembered entity, ball first, then players by id. A player
never observes himself.

| Field        | Meaning                                                            |
|--------------|--------------------------------------------------------------------|
| `entity`     | `ObservedEntity::ball()` or `ObservedEntity::player(id)`           |
| `position`   | where the entity was when last seen, in meters                     |
| `velocity`   | how it moved when last seen, in meters per second                  |
| `confidence` | 1 when seen, falling to 0 as the observation ages                  |
| `lastSeen`   | the tick of the last sighting                                      |

A state created from a spec starts with empty memories. Memories are part of
the state hash, so replays check them like positions.

### Update frequency

Perception runs every `intervalTicks` ticks, 3 by default: ten times a second
at 30 Hz, on ticks 0, 3, 6, … Like every system it reads the state at the start
of the tick, so it does not depend on rendering or on how often a caller steps.

### Seeing

`canSee(observer, position, config)` decides whether a player sees a position:

1. **Range.** Nothing beyond `viewDistance` (60 m).
2. **Awareness.** Anything within `awarenessRadius` (3 m), in every direction —
   a player knows who is at his back.
3. **Vision cone.** Otherwise the position must lie within half the field of
   view (`fieldOfViewDegrees`, 180°) of his facing:
   `facing · offset ≥ |offset| · cos(fov / 2)`.

Both bounds are inclusive. Players do not hide one another yet: no occlusion.
[Player movement](#player-movement) sets the facing: along his run while
running, toward the ball otherwise.

The perception system looks along `lookDirection()` (`carrierScan.hpp`): the
facing, with two exceptions. A carrier looking over his shoulder for an option
looks along his scan's gaze, while body and ball stay put
([pass decisions](on-ball.md#without-a-valid-option)). A carrier whose ball
rolls ahead after a touch looks along his run's gaze for his next decision
([ball carrying](on-ball.md#looking-ahead)).

The cone compares against `cos(fov / 2) − 10⁻⁹`. `std::cos` may differ in the
last bit between libraries, enough to make a replay diverge on an entity at the
edge. Perception therefore computes the cosine with basic arithmetic: a Taylor
series of `cos(fov / 4)`, doubled with `cos(2x) = 2·cos²(x) − 1`, accurate to a
few 10⁻¹⁶ and bit-identical everywhere, once per observer and update. The
tolerance keeps the edge inclusive: `cos(π/2)` comes out as about 2·10⁻¹⁶, not
0, which would hide a player standing exactly sideways in the 180° cone.

### Remembering and forgetting

On each update, for every entity:

- **Seen:** a fresh observation with the true position and velocity,
  confidence 1 and `lastSeen` = now. Estimates are exact at sighting;
  perception errors come with cognitive capabilities.
- **Not seen, remembered:** position, velocity and `lastSeen` stay; confidence
  becomes `1 − age / memorySeconds`, with age the time since `lastSeen`.
- **Not seen for `memorySeconds` (3 s):** forgotten — the observation is
  removed.

`estimatePosition(observation, time, config)` is where the entity probably is
now: its last seen position moved along its last seen velocity for the time
since, for at most `extrapolationSeconds` (1 s). A player who ran out of sight
is expected to keep running for a moment, not forever.

### Configuration

`PerceptionConfig` is part of `MatchConfig` and of every replay:

| Field                  | Default | Meaning                                          |
|------------------------|---------|--------------------------------------------------|
| `intervalTicks`        | 3       | ticks between updates                            |
| `viewDistance`         | 60 m    | how far a player sees                            |
| `fieldOfViewDegrees`   | 180°    | width of the vision cone                         |
| `awarenessRadius`      | 3 m     | sensed in every direction                        |
| `memorySeconds`        | 3 s     | time until an unseen entity is forgotten         |
| `extrapolationSeconds` | 1 s     | how far estimates project an unseen entity       |

The system rejects an interval below one tick, a field of view outside
(0°, 360°], negative distances or extrapolation, a memory that is not positive,
and any non-finite value.

### What this is not

No attention, no scanning behavior, no occlusion, no observation noise yet. All
players perceive with the same configuration; individual perception comes with
cognitive capabilities.

## Spatial queries

The questions about space that perception, passing and ball pursuit ask: who is
near a point, and how soon can a player get somewhere. They are pure functions
of a `MatchState` in `sim-match` (`spatialQueries.hpp`).

### Nearby players

`findPlayersWithin(state, center, radius, filter)` returns every player within
`radius` meters of `center`, nearest first. `findNearestPlayer(state, center,
filter)` returns the nearest at any distance. Each result names the player's
index in `MatchState::players()`, his id and his distance.

- **Radius.** Inclusive: a player exactly `radius` meters away is found. A
  negative or NaN radius finds no one.
- **Filter.** `PlayerFilter` restricts the result to one side and can leave out
  one player, usually the one asking:

  ```cpp
  PlayerFilter{}                                  // everyone
  PlayerFilter::onSide(TeamSide::kAway)           // the away side
  PlayerFilter::onSide(side).except(playerId)     // playerId's teammates
  ```

- **Stable order.** Players at the same distance are ordered by id, not by
  their position in the state, so a system walking the result behaves the same
  however the squad is ordered; replays stay deterministic. Distances are
  compared squared, so the order is exact.

A linear scan over 14 players is cheaper than any spatial index. An index can
go behind this interface once profiling of 11v11 or the world simulation needs
it.

### Arrival time

`estimateArrivalSeconds(player, position, pitch)` is the seconds the player
needs to reach `position` at full effort, running through it instead of
stopping on it — what counts for reaching a ball. It uses the limits of
[player movement](#player-movement):

1. Only the part of his velocity toward the position counts, `v0`; negative
   when he runs away from it.
2. He accelerates at `acceleration` from `v0` to `maxSpeed`, covering
   `(maxSpeed² − v0²) / (2·acceleration)` meters.
3. A position within that distance is reached while accelerating,
   `d = v0·t + acceleration·t²/2`; beyond it, he runs the rest at `maxSpeed`.

From rest at the default limits, 2 m take 1 s and 30 m about 4.8 s. A position
he is already on takes 0 s.

**Unreachable positions.** A non-finite position or one off the pitch gets no
estimate: movement targets never lead off the pitch.

**Approximation.** The estimate leaves out the time to turn away from a
sideways run, so it is optimistic for a player moving elsewhere. Reaction time
is not modeled yet. A test runs the movement system past a position and
requires the estimate to match the tick he gets there.
