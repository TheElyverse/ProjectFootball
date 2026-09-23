# Match state

`MatchState` is the smallest complete picture of a match the simulation works
on: a pitch, two teams of players, and one ball. It lives in `sim-match`
(`matchState.hpp`, `kickoffScenario.hpp`) and depends only on `sim-core`.

Nothing in this document advances time. The state is what the fixed-timestep
loop reads and writes; the loop itself, player movement, and ball physics are
separate concerns.

## What a state holds

| Type               | Contents                                                                        |
|--------------------|---------------------------------------------------------------------------------|
| `MatchState`       | the `Pitch`, the players in order, the `BallState`, and the squad size per side |
| `PlayerMatchState` | `playerId`, `side`, `position`, `velocity`                                      |
| `BallState`        | `position`, `velocity`                                                          |
| `TeamSide`         | `kHome` or `kAway`                                                              |

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

| Rule                                              | Error code                 |
|---------------------------------------------------|----------------------------|
| `playersPerSide` is at least 1                    | `kInvalidPlayersPerSide`   |
| each side fields exactly `playersPerSide` players | `kWrongPlayerCountPerSide` |
| every player has a valid id                       | `kInvalidPlayerId`         |
| player ids are unique                             | `kDuplicatePlayerId`       |
| player positions are finite                       | `kNonFinitePlayerPosition` |
| player positions are on the pitch                 | `kPlayerOutsidePitch`      |
| player velocities are finite                      | `kNonFinitePlayerVelocity` |
| the ball position is finite                       | `kNonFiniteBallPosition`   |
| the ball is on the pitch                          | `kBallOutsidePitch`        |
| the ball velocity is finite                       | `kNonFiniteBallVelocity`   |

Errors arrive in a fixed order — squad size, then players by index, then the
ball — so a rejection reads the same way on every run and on every platform.
Each error carries a message naming the details its rule needs: the side
counts for a squad-size error, the player's index, id, and side plus the
offending value for a player error, and the offending value for a ball error:

```text
home has 7 players and away has 6, expected 7 per side
duplicate player id 4 at index 8, first seen at index 3
player at index 4 (id 5, home) is outside the 60 x 40 m pitch at (60.00000000000001, 20) m
```

Numbers are printed in their shortest round-trippable form (`std::format("{}")`),
not with a fixed precision. The boundary is inclusive, so a player at exactly
60 m is on the pitch; the example above is one ulp past the touchline, and a
fixed two-decimal format would have printed it as a point on the line. The
output is locale-independent, so messages match on every platform.

One defect produces one error. A non-finite position is reported as such and
not additionally as off-pitch, and an invalid id is not also counted as a
duplicate of the next invalid one.

Squad size is checked per side rather than as a total, because the total
follows from the two side counts. `playersPerSide` defaults to
`kDefaultPlayersPerSide` (7), the M0 iteration stage; 11 players a side is a
valid spec today, so growing past seven-a-side is a different argument, not a
different type.

## The seven-a-side kickoff fixture

`makeSevenASideKickoff(pitch)` builds the fixed starting scenario. It is a pure
function of the pitch: no seed, no random number generator, no clock. The same
pitch always produces the same state, which is what makes it usable as the
`InitialSnapshot` of a replay ([implementation plan](implementation-plan.md)
section 5.3).

Home defends `x = 0` and attacks `+x`. Positions are fractions of the pitch
dimensions rather than fixed meters, so the fixture fits any valid pitch:

| Id   | Side | Role            | x             | y            |
|------|------|-----------------|---------------|--------------|
| 1    | home | goalkeeper      | 0.05 · length | 0.50 · width |
| 2    | home | left back       | 0.22 · length | 0.22 · width |
| 3    | home | right back      | 0.22 · length | 0.78 · width |
| 4    | home | center midfield | 0.35 · length | 0.50 · width |
| 5    | home | left midfield   | 0.42 · length | 0.25 · width |
| 6    | home | right midfield  | 0.42 · length | 0.75 · width |
| 7    | home | forward         | 0.47 · length | 0.50 · width |
| 8–14 | away | mirror of 1–7   | length − x    | unchanged    |

Away is home's mirror image through the halfway line, so each side starts in
its own half. The ball rests on the center spot and every velocity is zero.
Role names describe the layout; they are not a field of the state, because
responsibilities belong to the tactics module.

Every fraction lies strictly between zero and one, so the fixture cannot fall
outside the pitch it was built for. It still goes through
`MatchState::create()`, so there is exactly one validated way to build a state.

The unit tests pin the resulting coordinates. Changing a fraction changes the
fixture, and a changed fixture invalidates every replay recorded against it —
treat the table above as a contract, not a default.

## What this is not

There is no clock, no movement, no ball physics, no possession, and no rules
here: no offside, no out of play, no fouls. `Pitch::contains()` decides whether
a position is on the rectangle, nothing more. A player standing on the goal
line is a valid state, not a goal.
