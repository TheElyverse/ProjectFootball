# Spatial queries

Shared questions about space that perception, passing and ball pursuit all ask:
who is near a point, and how soon can a player get somewhere. They live in
`sim-match` (`spatialQueries.hpp`) as pure functions of a `MatchState`.

## Nearby players

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

## Arrival time

`estimateArrivalSeconds(player, position, pitch)` is how many seconds the player
needs to reach `position` at full effort, running through it rather than stopping
on it — what matters for reaching a ball. It uses the limits of
[player movement](player-movement.md):

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
