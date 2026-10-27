# Players

How a player moves, what he perceives of the match, and the spatial questions
every decision asks: who is near, and how soon can someone get somewhere.

- [Player movement](#player-movement)
- [Perception](#perception)
- [Spatial queries](#spatial-queries)

## Player movement

Players move toward targets assigned by commands. The movement model lives in
`sim-match` (`playerMovement.hpp`) and plugs into the [match loop](match-engine.md#match-loop)
as the system `makePlayerMovementSystem()`, which runs every tick and writes
player positions, velocities and facings. A goalkeeper busy with a dive is the
exception: the system puts him where his dive takes him, whatever his target
([shot stopping](goalkeeper.md#his-body)). A player off his feet after a
[duel](duels.md#off-his-feet) moves as if he had no target: he slows to a
stop (`asMoving()`).

### Targets

A `MovePlayerCommand` sets a player's `target` (see [match loop](match-engine.md#match-loop),
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

### Limits

Every player has two `PlayerAttributes`, fixed for the match:

| Attribute      | Default | Meaning                                                   |
|----------------|---------|-----------------------------------------------------------|
| `maxSpeed`     | 7.5 m/s | the speed a player never exceeds                          |
| `acceleration` | 4 m/s²  | the largest change of velocity per second, in any direction |

One acceleration limits speeding up, slowing down and turning alike. The values
describe the predefined test players of the sandbox; generated players will
derive theirs from capabilities.

**With the ball** at his feet or rolling ahead of him after a touch, a player
runs no faster than `runningSpeed()`: his `maxSpeed` times a share between
`BallPhysics::minCarrySpeedFactor` (0.8) and `maxCarrySpeedFactor` (0.95) by
his `ballControl` ([ball carrying](on-ball.md#running-with-the-ball)). The
movement system and every prediction of his move use `stepPlayerMovement()` at
that speed.

### One tick of movement

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

Lengths use `Vec2::length()`, which is built on `SimCore::stableHypot()`
rather than `std::hypot`: it uses only correctly rounded IEEE 754 operations, so
the result does not depend on the platform's math library.

### Facing

The movement system also turns players, with `facingAfterMove()`:

- a player moving faster than `kFacingRunSpeed` (1 m/s) looks where he runs,
- a slower or standing player looks at the ball -- a goalkeeper holding it up
  the pitch ([distribution](goalkeeper.md#the-ball-in-his-hands)),
  and a carrier at the ball where the systems before movement left it: in a
  [duel](duels.md#duels-on-the-ball) he rolls it round him, and turns with it,
- a player standing exactly on the ball keeps his facing.

He turns at once. A turning rate, and looking elsewhere than where he runs,
belong to later body-orientation work. Facing decides what a player can see; see
[perception](#perception).

### Example

From rest, a player covers 20 m in about 4.5 s at the default limits: 1.9 s to
reach 7.5 m/s over 7 m, 0.8 s at full speed, and 1.9 s braking over the last 7 m.

### What this is not

There is no reaction time, no fatigue, no turning rate, and no collision
between players: two players can stand on the same spot. Off-ball positioning
— where a player should go — is for later tactical systems; this model only
decides how he gets there.

## Perception

Players do not know the true match state. Each player keeps a memory of what he
has seen — the ball and other players — and every decision reads that memory,
never the state behind it. Perception lives in `sim-match` (`perception.hpp`,
`observation.hpp`) and plugs into the [match loop](match-engine.md#match-loop) as
`makePerceptionSystem(PerceptionConfig)`.

### What a player remembers

`MatchState::perception(playerIndex)` is a `PlayerPerception`: one `Observation`
per entity the player remembers, ordered ball first and then players by id. A
player never observes himself.

| Field        | Meaning                                                            |
|--------------|--------------------------------------------------------------------|
| `entity`     | `ObservedEntity::ball()` or `ObservedEntity::player(id)`           |
| `position`   | where the entity was when last seen, in meters                     |
| `velocity`   | how it moved when last seen, in meters per second                  |
| `confidence` | 1 when seen, falling to 0 as the observation ages                  |
| `lastSeen`   | the tick of the last sighting                                      |

Every state created from a spec starts with empty memories. Memories are part of
the state hash, so replays check them like positions.

### Update frequency

Perception runs every `intervalTicks` ticks, 3 by default: ten times a second at
30 Hz, on ticks 0, 3, 6, … It reads the state at the start of the tick like every
system, so it is independent of rendering and of how often a caller steps.

### Seeing

`canSee(observer, position, config)` decides whether a player sees a position:

1. **Range.** Nothing farther than `viewDistance` (60 m) is seen.
2. **Awareness.** Anything within `awarenessRadius` (3 m) is sensed in every
   direction — a player knows who is at his back.
3. **Vision cone.** Otherwise the position must lie within half the field of view
   (`fieldOfViewDegrees`, 180°) of the player's facing:
   `facing · offset ≥ |offset| · cos(fov / 2)`.

Both bounds are inclusive. Players do not hide one another yet: there is no
occlusion. What a player faces is set by [player movement](#player-movement):
along his run while running, toward the ball otherwise.

The perception system looks along `lookDirection()` (`carrierScan.hpp`)
instead: the facing, except for a carrier who looks over his shoulder for a
passing option -- then along his scan's gaze, while his body and the ball
stay as they are ([pass decisions](on-ball.md#without-a-valid-option)) --
and for a carrier whose ball rolls ahead of him after a touch, who looks about
for his next decision along his run's gaze
([ball carrying](on-ball.md#looking-ahead)).

The cone compares against `cos(fov / 2) − 10⁻⁹`. The cosine does not come from
`std::cos`: the standard does not require it to be correctly rounded, and
standard libraries may differ in the last bit — enough to decide differently
for an entity right on the edge and make a replay diverge between machines.
Perception computes it from basic arithmetic instead: a Taylor series of
`cos(fov / 4)`, doubled with `cos(2x) = 2·cos²(x) − 1`, accurate to a few 10⁻¹⁶
and bit-identical everywhere. It is computed once per observer and update. The
tolerance keeps the edge inclusive: `cos(π/2)` comes out as about 2·10⁻¹⁶
rather than 0, which would otherwise hide a player standing exactly sideways in
the default 180° cone.

### Remembering and forgetting

On each update, for every entity:

- **Seen:** a fresh observation with the entity's true position and velocity,
  confidence 1 and `lastSeen` = now. Estimates are exact at sighting; perception
  errors come with cognitive capabilities.
- **Not seen, remembered:** position, velocity and `lastSeen` stay as they were;
  confidence becomes `1 − age / memorySeconds`, where age is the time since
  `lastSeen`.
- **Not seen for `memorySeconds` (3 s):** forgotten — the observation is removed.

`estimatePosition(observation, now, secondsPerTick, config)` is where the entity
probably is now: its last seen position moved along its last seen velocity for
the time since, but for at most `extrapolationSeconds` (1 s). A player who ran
out of sight is expected to have kept running for a moment, not forever.

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

There is no attention, no scanning behavior, no occlusion and no observation
noise yet. All players perceive with the same configuration; individual
perception belongs to cognitive capabilities.

## Spatial queries

Shared questions about space that perception, passing and ball pursuit all ask:
who is near a point, and how soon can a player get somewhere. They live in
`sim-match` (`spatialQueries.hpp`) as pure functions of a `MatchState`.

### Nearby players

`findPlayersWithin(state, center, radius, filter)` returns every player within
`radius` meters of `center`, nearest first; `findNearestPlayer(state, center,
filter)` returns the nearest one at any distance. Each result names the player's
index in `MatchState::players()`, his id and his distance.

- **Radius.** Inclusive: a player exactly `radius` meters away is found. A
  negative or NaN radius finds no one.
- **Filter.** `PlayerFilter` restricts the result to one side and can leave out
  one player, typically the one asking:

  ```cpp
  PlayerFilter{}                                  // everyone
  PlayerFilter::onSide(TeamSide::kAway)           // the away side
  PlayerFilter::onSide(side).except(playerId)     // playerId's teammates
  ```

- **Stable order.** Players at the same distance are ordered by id, not by their
  position in the state. A system that walks the result behaves the same however
  the squad is ordered, which keeps replays deterministic. Distances are compared
  squared, so the ordering is exact.

A linear scan over 14 players is cheaper than any spatial index. An index belongs
behind this interface once profiling of 11v11 or world simulation asks for it.

### Arrival time

`estimateArrivalSeconds(player, position, pitch)` is how many seconds the player
needs to reach `position` at full effort, running through it rather than stopping
on it — what matters for reaching a ball. It uses the limits of
[player movement](#player-movement):

1. Only the part of his velocity that points at the position counts, `v0`;
   negative when he is running away from it.
2. He accelerates at `acceleration` from `v0` to `maxSpeed`, covering
   `(maxSpeed² − v0²) / (2·acceleration)` meters.
3. A position within that distance is reached during acceleration,
   `d = v0·t + acceleration·t²/2`; beyond it, the rest is run at `maxSpeed`.

From rest at the default limits, 2 m take 1 s and 30 m about 4.8 s. A position
the player is already on takes 0 s.

**Unreachable positions.** A position that is not finite or lies off the pitch
returns no estimate: movement targets never lead off the pitch, so no player can
be sent there.

**Approximation.** Turning away from a sideways run costs time the estimate does
not include, so it is optimistic for a player whose velocity points elsewhere.
Reaction time is not modeled yet. A test runs the movement system past a
position and requires the estimate to match the tick the player gets there.
