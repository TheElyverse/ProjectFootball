# Match state

`MatchState` is the smallest complete picture of a match the simulation works
on: a pitch, two teams of players, and one ball. It lives in `sim-match`
(`matchState.hpp`) and depends only on `sim-core`.

Nothing in this document advances time. The state is what the fixed-timestep
loop reads and writes; the loop itself, player movement, and ball physics are
separate concerns.

## What a state holds

| Type | Contents |
| --- | --- |
| `MatchState` | the `Pitch`, the players in order, the `BallState`, and the squad size per side |
| `PlayerMatchState` | `playerId`, `side`, `position`, `velocity` |
| `BallState` | `position`, `velocity` |
| `TeamSide` | `kHome` or `kAway` |

Positions are meters in pitch coordinates, velocities are meters per second, in
the plane described by [match geometry](match-geometry.md). The pitch
coordinate system is fixed, so `TeamSide` says which squad a player belongs to,
not which way that squad attacks; a scenario decides which side defends
`x = 0`.

The fields are deliberately few. Orientation, energy, action, perception, and
tactical runtime state from [implementation plan](implementation-plan.md)
section 6.2 arrive with the systems that fill them, and the ball's third
dimension and spin (section 6.7) arrive with the passing model.

Player order is part of the state. Two states holding the same players in a
different order are not equal, because the order in which players are updated
has to stay fixed for a replay to reproduce.

## Construction is validation

`MatchState::create(MatchStateSpec)` is the only way to obtain a `MatchState`,
so every instance that exists has passed every rule below. It returns
`std::expected<MatchState, std::vector<MatchStateError>>`: on success the
state, on failure every rule the spec breaks, not just the first one.

| Rule | Error code |
| --- | --- |
| `playersPerSide` is at least 1 | `kInvalidPlayersPerSide` |
| each side fields exactly `playersPerSide` players | `kWrongPlayerCountPerSide` |
| every player has a valid id | `kInvalidPlayerId` |
| player ids are unique | `kDuplicatePlayerId` |
| player positions are finite | `kNonFinitePlayerPosition` |
| player positions are on the pitch | `kPlayerOutsidePitch` |
| player velocities are finite | `kNonFinitePlayerVelocity` |
| the ball position is finite | `kNonFiniteBallPosition` |
| the ball is on the pitch | `kBallOutsidePitch` |
| the ball velocity is finite | `kNonFiniteBallVelocity` |

Errors arrive in a fixed order — squad size, then players by index, then the
ball — so a rejection reads the same way on every run and on every platform.
Each error carries a message naming the offending index, id, and value:

```text
home has 7 players and away has 6, expected 7 per side
duplicate player id 4 at index 8, first seen at index 3
player at index 4 (id 5, home) is outside the 60.00 x 40.00 m pitch at (60.00, 20.00) m
```

One defect produces one error. A non-finite position is reported as such and
not additionally as off-pitch, and an invalid id is not also counted as a
duplicate of the next invalid one.

Squad size is checked per side rather than as a total, because the total
follows from the two side counts. `playersPerSide` defaults to
`kDefaultPlayersPerSide` (7), the M0 iteration stage; 11 players a side is a
valid spec today, so growing past seven-a-side is a different argument, not a
different type.

## What this is not

There is no clock, no movement, no ball physics, no possession, and no rules
here: no offside, no out of play, no fouls. `Pitch::contains()` decides whether
a position is on the rectangle, nothing more. A player standing on the goal
line is a valid state, not a goal.
