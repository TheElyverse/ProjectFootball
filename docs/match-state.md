# Match state

`MatchState` is the smallest complete picture of a match the simulation works
on: a pitch, two teams of players, and one ball. It lives in `sim-match`
(`matchState.hpp`, `kickoffScenario.hpp`) and depends on `sim-core` and, for
the tactics sides play, `sim-tactics`.

Nothing in this document advances time. The state is what the
[match loop](match-loop.md) reads and writes; the loop itself, player movement,
and ball physics are separate concerns.

## What a state holds

| Type               | Contents                                                                        |
|--------------------|---------------------------------------------------------------------------------|
| `MatchState`       | the `Pitch`, the players in order, the `BallState`, the squad size per side, every player's perception memory, and each side's tactic |
| `PlayerMatchState` | `playerId`, `side`, `position`, `velocity`, `attributes`, `target`, `facing`    |
| `PlayerAttributes` | `maxSpeed` (m/s) and `acceleration` (m/s²), fixed for the match                 |
| `BallState`        | `position`, `velocity`, `owner`, `lastTouch`                                    |
| `TeamSide`         | `kHome` or `kAway`                                                              |

Positions are meters in pitch coordinates, velocities are meters per second, in
the plane described by [match geometry](match-geometry.md). The pitch
coordinate system is fixed, so `TeamSide` says which squad a player belongs to,
not by itself which way that squad attacks. The sandbox has no halves yet, so
home defends `x = 0` and attacks `+x` in every match; systems that need the
direction take it from `attackingDirection()`, the one place to change once
rules let teams switch ends.

`target` is where the player is moving to. It is empty until a command assigns
one, and without a target a player comes to a stop where he is; see
[player movement](player-movement.md). `attributes` default to `kDefaultMaxSpeed` (7.5 m/s) and
`kDefaultAcceleration` (4 m/s²); they describe the predefined test players of the
sandbox, not a generated player.

The ball's `owner` is the player in control of it, empty while it is free, and
`lastTouch` the last player to kick or take it; see [possession](possession.md).
A state also holds the pass a player has decided on and not yet played,
`pendingPass()`, empty in every state created from a spec; see
[passing](passing.md).

`facing` is the unit vector a player looks along; it decides what he can see. It
is a vector rather than an angle so that no trigonometry, and none of its
platform differences, enters the state. The kickoff fixture turns each side
toward the goal it attacks.

Each side plays a [tactic](tactics.md) or none: `MatchState::create(spec,
tactics)` takes them as `TeamTactics { home, away }`, both empty by default. A
side without a tactic is scripted -- its players move only on commands and after
free balls, as in the P1 sandbox. A tactic must have one slot per player of its
side (`kTacticDoesNotFitSquad` otherwise); the player in slot `i` is the `i`-th
player of his side in `players()`, and `slotIndex(state, playerIndex)` finds it.
The state hash includes each tactic by its content hash.

The state also holds which team has the ball, `possession()` -- the team, the
tick it won the ball and whether it won it from the opponent -- and the
tactical phase of each side with a tactic, `phase(side)`. Both are empty in a
state created from a spec and kept up to date by the tactical phase system; see
[match phases](match-phases.md). Likewise `pitchControl()` caches the
[pitch-control](pitch-control.md) grid between refreshes, `chaser(side)` names the
player each side has sent after a free ball ([reception](reception.md)), and
`tactical(playerIndex)` holds each player's tactical runtime state, such as his
[desired region](desired-region.md), and `press(side)` the side's
[press](pressing.md) in progress. `lastPass()` and `lastReception()` record the
last pass kicked and the last free ball controlled. All are empty in a state
created from a spec.

Every player also has a perception memory, `perception(playerIndex)`: what he
believes about the ball and the other players (see [perception](perception.md)).
A state created from a spec starts with every memory empty — perception is built
up by the match, not given — so the spec and the replay format need no field
for it; the state hash includes it.

The fields are deliberately few. Orientation, energy, action, perception, and
tactical runtime state from [implementation plan](implementation-plan.md)
section 6.2 arrive with the systems that fill them, and the ball's third
dimension and spin (section 6.7) arrive with the passing model.

Player order is part of the state. Two states holding the same players in a
different order are not equal, because the order in which players are updated
has to stay fixed for a replay to reproduce.

## Construction is validation

`MatchState::create(MatchStateSpec)` is the only way to obtain a `MatchState`
from outside the simulation, so every instance that exists has passed every rule
below. It returns `std::expected<MatchState, std::vector<MatchStateError>>`: on
success the state, on failure every rule the spec breaks, not just the first
one.

| Rule                                              | Error code                 |
|---------------------------------------------------|----------------------------|
| `playersPerSide` is at least 1                    | `kInvalidPlayersPerSide`   |
| each side fields exactly `playersPerSide` players | `kWrongPlayerCountPerSide` |
| every player's side is home or away               | `kInvalidTeamSide`         |
| every player has a valid id                       | `kInvalidPlayerId`         |
| player ids are unique                             | `kDuplicatePlayerId`       |
| player attributes are positive and finite         | `kInvalidPlayerAttributes` |
| player positions are finite                       | `kNonFinitePlayerPosition` |
| player velocities are finite                      | `kNonFinitePlayerVelocity` |
| no player moves faster than his max speed         | `kPlayerTooFast`           |
| player targets, where set, are finite             | `kNonFinitePlayerTarget`   |
| player facings are finite unit vectors            | `kInvalidPlayerFacing`     |
| the ball position is finite                       | `kNonFiniteBallPosition`   |
| the ball velocity is finite                       | `kNonFiniteBallVelocity`   |
| the ball is not faster than `kMaxBallSpeed` (100 m/s) | `kBallTooFast`         |
| the ball's owner, where set, is a player in the state | `kUnknownBallOwner`    |
| the ball's last touch, where set, is a player in the state | `kUnknownLastTouch` |

These rules hold for every state of a match, from kickoff to the final whistle.
The match loop only changes positions, velocities, targets and facings, and
checks the state it writes with `findNonFiniteValues(const MatchState&)`, which
applies the five finiteness rules and the facing rule with the same codes and
messages as `create()`. A facing counts as a unit vector when its squared length
is within `kFacingTolerance` (1e-9) of one.
Being on the pitch is deliberately not one of them: a ball that crossed the
touchline or a player standing behind the goal line is football, not a broken
state. Deciding what such a position means — a throw-in, a goal kick, a goal —
belongs to the rules system, not to validation.

## Starting positions

A state a match starts from, such as the kickoff fixture below, must also have
everyone on the pitch. `checkStartingPositions(const MatchState&)` checks that
and returns one error per offender, players by index and then the ball, or an
empty list:

| Rule                              | Error code            |
|-----------------------------------|-----------------------|
| player positions are on the pitch | `kPlayerOutsidePitch` |
| the ball is on the pitch          | `kBallOutsidePitch`   |

## Error messages

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

One defect produces one error. An invalid id is not also counted as a
duplicate of the next invalid one, and a player with an undeclared side does
not also break the squad-size rule when assigning it a side could satisfy it.

Squad size is checked per side rather than as a total, because the total
follows from the two side counts. `playersPerSide` defaults to
`kDefaultPlayersPerSide` (7), the M0 iteration stage; 11 players a side is a
valid spec today, so growing past seven-a-side is a different argument, not a
different type.

## The seven-a-side kickoff fixture

`makeSevenASideKickoff(pitch, ballVelocity = {})` builds the fixed starting
scenario. It is a pure function of its arguments: no seed, no random number
generator, no clock. The same arguments always produce the same state, which is
what makes it usable as the `InitialSnapshot` of a replay
([implementation plan](implementation-plan.md) section 5.3).

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
its own half. The ball lies on the center spot and every player velocity is
zero. The ball is at rest unless the optional `ballVelocity` argument sets it
rolling; see [ball movement](ball-movement.md).
Role names describe the layout; they are not a field of the state, because
responsibilities belong to the tactics module.

Every fraction lies strictly between zero and one, so the fixture cannot fall
outside the pitch it was built for. It still goes through
`MatchState::create()` and `checkStartingPositions()`, so there is exactly one
validated way to build a state and the fixture is held to the same rules as any
other starting state.

The unit tests pin the resulting coordinates. Changing a fraction changes the
fixture, and a changed fixture invalidates every replay recorded against it —
treat the table above as a contract, not a default.

## What this is not

The state does not advance itself: the clock is the [match loop](match-loop.md)'s,
and movement and ball physics are systems that run in it. There is no possession
and there are no rules here: no offside, no out of play, no fouls. `Pitch::contains()` decides whether
a position is on the rectangle, nothing more. A player standing on the goal
line is a valid state, and so is a ball behind it; whether that ball is a goal
is for the rules to decide.
