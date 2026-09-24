# Perception

Players do not know the true match state. Each player keeps a memory of what he
has seen — the ball and other players — and every decision reads that memory,
never the state behind it. Perception lives in `sim-match` (`perception.hpp`,
`observation.hpp`) and plugs into the [match loop](match-loop.md) as
`makePerceptionSystem(PerceptionConfig)`.

## What a player remembers

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

## Update frequency

Perception runs every `intervalTicks` ticks, 3 by default: ten times a second at
30 Hz, on ticks 0, 3, 6, … It reads the state at the start of the tick like every
system, so it is independent of rendering and of how often a caller steps.

## Seeing

`canSee(observer, position, config)` decides whether a player sees a position:

1. **Range.** Nothing farther than `viewDistance` (60 m) is seen.
2. **Awareness.** Anything within `awarenessRadius` (3 m) is sensed in every
   direction — a player knows who is at his back.
3. **Vision cone.** Otherwise the position must lie within half the field of view
   (`fieldOfViewDegrees`, 180°) of the player's facing:
   `facing · offset ≥ |offset| · cos(fov / 2)`.

Both bounds are inclusive. Players do not hide one another yet: there is no
occlusion. What a player faces is set by [player movement](player-movement.md):
along his run while running, toward the ball otherwise.

The cone compares against `cos(fov / 2) − 10⁻⁹`, computed with `std::cos` from
the configured degrees. The tolerance keeps the edge inclusive: `cos(π/2)` comes
out as about 6·10⁻¹⁷ rather than 0, which would otherwise hide a player standing
exactly sideways in the default 180° cone. It also absorbs a standard library
rounding that cosine differently in the last bit; only an entity within 10⁻⁹
radians of the edge could still notice.

## Remembering and forgetting

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

## Configuration

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

## What this is not

There is no attention, no scanning behavior, no occlusion and no observation
noise yet. All players perceive with the same configuration; individual
perception belongs to cognitive capabilities.
