# Match engine

The frame every match system runs in: the pitch and its coordinates, the
validated match state, the fixed-timestep loop with its commands and systems,
the events a step publishes, and how play restarts after the ball goes out.

- [Header layers](#header-layers)
- [Match geometry](#match-geometry)
- [Match state](#match-state)
- [Match loop](#match-loop)
- [Match events and decision diagnostics](#match-events-and-decision-diagnostics)
- [Restarts](#restarts)

## Header layers

The public headers of `sim-match` form five layers. A header includes
`sim-match` headers of its own layer or a lower one only, so the match state
knows nothing of the systems that change it, and the loop nothing of the
systems it runs:

| Layer     | What it holds                                                    | Headers, for example                                  |
|-----------|------------------------------------------------------------------|-------------------------------------------------------|
| model     | the match state and its parts, events, commands, candidate types | `matchState.hpp`, `matchEvents.hpp`, `passCandidate.hpp` |
| mechanics | pure physics, geometry and execution, spatial queries            | `ballPhysics.hpp`, `passing.hpp`, `reception.hpp`     |
| loop      | the fixed-timestep loop and the interface of its systems         | `matchSimulation.hpp`                                 |
| behaviour | the decisions and the systems that run them                      | `passDecision.hpp`, `ballMovement.hpp`, `duel.hpp`    |
| setup     | configuration and fixtures that put whole matches together       | `matchSetup.hpp`, `scenarios.hpp`                     |

`tests/architecture/headerLayers.cmake` lists every header's layer and fails
the `sim-match-header-layers` test for a header that includes one above its
own, or that belongs to no layer: a new header needs its layer there. Sources
under `src/` may include anything.

A type an event carries belongs with the events, even when one system makes
it: the goalkeeper's `SweepCall` and the aerial duel's `AerialPlay` are model,
so the loop, which records events, does not depend on the goalkeeper or the
aerial duels.

Systems share one shape. A factory `make…System()` takes the system's own
configuration, or a `…Rules` struct when it needs more than one, whose fields
default to the standard configuration. The time a rule needs is a
`SimCore::StepTime`, which `MatchStepContext::time()` hands out. A system with
many steps plays them as const methods of one object that holds the rules, the
context, the state and the writer for the step, as `BallTick` and `DuelStep`
do, rather than passing all four from function to function.

## Match geometry

`sim-match` is a standard C++23 static library with no Unreal dependency.
Consumers link `ElyverseFootball::sim-match` and include `pitch.hpp`.
The reusable `Vec2` value type lives in `sim-core` (`vec2.hpp`).

### Illustrated example

![Pitch construction, coordinate bounds, and vector calculations on a 60 by 40 meter field](images/pitch-geometry.svg)

[Editable vector drawing](images/pitch-geometry.svg) · [PNG version](images/pitch-geometry.png)

The example connects `A = (10, 10)` to `B = (40, 30)`. Subtracting the
positions gives the displacement `(30, 20)` meters. Its length is
`(30, 20).length()`, approximately 36.06 meters. A velocity of `(6, 4)`
meters per second multiplied by five seconds gives the same displacement;
this illustrates vector arithmetic, not an implemented movement system.

`Pitch` stores only the two dimensions. Grid lines, markings, the center,
and example points in the drawing illustrate the coordinate system; they are
not additional stored geometry -- the markings below are derived from the
dimensions on demand. Boundary checks include a point on the edge
and reject a point beyond it, without applying ball radius or football rules.

### Coordinate system

- Positions and pitch dimensions are measured in meters.
- The origin `(0, 0)` is one corner of the pitch.
- The x axis follows the pitch length; goal lines are at `x = 0` and `x = length`.
- The y axis follows the pitch width; touchlines are at `y = 0` and `y = width`.
- The center is `(length / 2, width / 2)`.
- Coordinates stay fixed when teams change ends. Team attacking direction is a
  separate concern. Renderers map these coordinates to their own screen axes.

`Pitch(60.0, 40.0)` creates a 60 by 40 meter rectangle. These are example
dimensions, not a mandated 7v7 size. Both dimensions must be positive and finite;
invalid dimensions throw `std::invalid_argument`.

Two `Pitch` values compare equal when both dimensions match exactly; like `Vec2`,
the comparison applies no tolerance.

`Pitch::contains()` includes the edges and corners, without an implicit epsilon.
It returns false for positions containing NaN or infinity. It tests a point,
not the full ball: out-of-play rules, ball radius, and player movement constraints
are not part of this geometry API.

`Pitch::clamp()` returns the point on the pitch nearest to a finite position:
the position itself when `contains()` holds, otherwise its projection onto the
nearest edge or corner. Movement commands use it to keep targets on the pitch.

### Named sizes

Two sizes are named, and neither is imposed: any positive, finite pair of
dimensions builds a pitch.

- `kStandardLengthMeters` (105) and `kStandardWidthMeters` (68): standard
  eleven-a-side dimensions, the reference pitch the Laws of the Game state every
  marking for.
- `kSandboxLengthMeters` (60) and `kSandboxWidthMeters` (40): the sandbox size
  the seven-a-side fixtures and scenarios play on -- small enough to read in the
  debug viewer, not a mandated 7v7 size.

### Markings

`Pitch::markings()` derives the lengths every marking is built from, in meters,
from the two dimensions alone. `Pitch` stores nothing but the dimensions: the
markings are computed on demand, two pitches with the same dimensions have the
same markings, and `operator==` therefore still compares the dimensions only.

The Laws fix every marking and give a range only for the pitch itself, so the
dimensions are the only thing to configure. `kStandardMarkings` holds the Laws'
own numbers, which a pitch of another size scales:

| Marking | At the standard pitch | Scales with |
| --- | --- | --- |
| `goalWidthMeters` | 7.32 | the width |
| `goalHeightMeters` | 2.44 | the width |
| `penaltyAreaDepthMeters` | 16.5 | the length |
| `penaltyAreaWidthMeters` | 40.32 | the width |
| `goalAreaDepthMeters` | 5.5 | the length |
| `goalAreaWidthMeters` | 18.32 | the width |
| `penaltySpotDistanceMeters` | 11 | the length |
| `centerCircleRadiusMeters` | 9.15 | the smaller factor |
| `cornerArcRadiusMeters` | 1 | the smaller factor |

The penalty area is 16.5 meters deep and reaches 16.5 meters to either side of
the goal (16.5 + 7.32 + 16.5 = 40.32); the goal area does the same with 5.5
(5.5 + 7.32 + 5.5 = 18.32).

Depths scale with the length, widths across the pitch with the width, and the
two radii with the smaller of the two factors, so a circle stays a circle
instead of becoming an ellipse. Because each standard marking is smaller than
the standard pitch in the direction it scales with, every derived marking fits
inside its pitch, however small or lopsided: the 60 by 40 meter sandbox pitch
gets a 4.31 by 1.44 meter goal, a penalty area 9.43 meters deep and 23.72 wide,
and a center circle of radius 5.23. At 105 by 68 both factors are exactly one,
so the markings are the Laws' numbers bit for bit.

Markings are geometry, not rules. Nothing in the simulation treats a line as a
boundary by itself; offside, penalties and goals are rules built on top of this
geometry, and none of them exists yet.

### Goals, areas, spots and arcs

Both goals are named by the goal line they stand on, `GoalEnd::kMinX` for
`x = 0` and `GoalEnd::kMaxX` for `x = length`. Pitch coordinates stay fixed when
teams change ends, so an end says nothing about which side defends it.

- `goalLineX(end)` is 0 or the length; `center()` is the middle of the pitch.
- `goal(end)` is the frame: the middle between the posts on the goal line, the width
  between the posts, and the height to the underside of the crossbar.
  `postAtMinY()` and `postAtMaxY()` are the two posts, at the lower and the
  higher pitch y. Posts and crossbar have no thickness of their own.
- `penaltyArea(end)` and `goalArea(end)` are `PitchRect`s reaching from the goal
  line into the pitch, centered on the goal; `penaltySpot(end)` lies on the
  goal's center line.
- `centerCircle()` is a `PitchCircle`. `cornerArc(corner)` is the full circle
  the arc is a quarter of: a position is in the arc when it is inside that
  circle and on the pitch. `PitchCorner` names a corner by the goal line and the
  touchline it lies on, `cornerPosition(corner)` gives the corner itself.

`PitchCircle::contains()` includes the edge and rejects non-finite points, the
way `PitchRect::contains()` and `Pitch::contains()` do. It compares the
`length()` of the offset against the radius rather than
their squares, so it stays correct on the extreme pitch sizes where squaring
would overflow or underflow. Heights are meters above the ground. The pitch geometry stores none of its own:
a caller supplies the height it asks about, and the ball carries its own
([ball movement](ball.md#ball-movement)).

`Goal::framesPoint(pitchY, height)` answers whether a point is inside the frame:
`isBetweenPosts()` and `isUnderCrossbar()` both hold, so a point beside a post
or above the crossbar misses. Both edges are included -- a point exactly on a
post or against the underside of the crossbar is framed -- and neither says
anything about whether a ball crossed the line or which way it travelled. That
is for shot and rule code to decide.

### Distance and open angle to a goal

Three helpers exist for shot and goalkeeper code:

- `isInPenaltyArea(end, position)` includes the area's lines, like every other
  containment check here, and is false for a non-finite position.
- `distanceToGoalMeters(end, position)` is the distance to the nearest point of
  the goal -- the line between the posts, not its center -- so it is zero
  between the posts and runs to the nearer post from beside it. A non-finite
  position gives a non-finite distance.
- `goalAngleRadians(end, position)` is the angle in `[0, pi]` that the goal
  subtends at a position: how much goal there is to shoot at. It is widest in
  front of the center, narrows toward the posts and with distance, is `pi`
  between the posts and zero on a post or for a non-finite position. It
  is the open angle of an empty pitch -- nobody blocks it -- and a position
  behind the goal line gets the same angle as its mirror image, so callers check
  where they stand themselves.

The angle uses `SimCore::stableArcTangent()` rather than `std::atan2`, for the
same reason perception computes its own cosine and `sim-core` its own
exponential: the standard functions need not be correctly rounded, and a
last-bit difference between two machines would be enough to rate the same shot
differently and make a replay diverge. `stableArcTangent()` works from basic
arithmetic and `std::sqrt`, both correctly rounded, and is accurate to a few
units in the last place.

`Vec2::length()`, and with it `SimCore::distance()`, the goal distance and
the circles' containment, uses `SimCore::stableHypot()` rather than `std::hypot`:
that too need not be correctly rounded. `stableHypot()` is exactly
`std::sqrt(x * x + y * y)` wherever the squares are safe and scales both
components by an exact power of two first where they would overflow or
underflow.

### Vector operations

`Vec2` stores two doubles by value and defaults to zero. It supports addition,
subtraction, scalar multiplication, dot product, length, squared length,
distance, and explicit finite-value checks. It has no owning pointers, dynamic
allocation, or indexed component access. Units follow the context: a position
uses meters, a velocity uses meters per second, and velocity multiplied by
seconds yields displacement.

Equality compares components exactly. Geometric tolerance checks should use an
explicit, context-specific tolerance. Arithmetic follows floating-point rules
and may produce non-finite results for extreme inputs; `isFinite()` allows those
results to be detected. Length uses `SimCore::stableHypot()`, which is
bit-identical on every platform and avoids intermediate overflow or underflow
when squaring components.

`lengthSquared()` returns the squared magnitude without the square root. Its unit
is the square of the component unit: meters squared for a position in meters.
Compare it against a squared threshold, never against a length. It orders
magnitudes exactly as `length()` does, and is exact where `length()` rounds, which
suits proximity and radius comparisons. Unlike `length()`, it squares the
components directly, so extreme inputs can overflow to infinity.

These primitives introduce no randomness. They do not promise bitwise-identical
floating-point results across different compilers or platforms.

## Match state

`MatchState` is the smallest complete picture of a match the simulation works
on: a pitch, two teams of players, and one ball. It lives in `sim-match`
(`matchState.hpp`, `kickoffScenario.hpp`) and depends on `sim-core` and, for
the tactics sides play, `sim-tactics`.

Nothing in this chapter advances time. The state is what the
[match loop](#match-loop) reads and writes; the loop itself, player movement,
and ball physics are separate concerns.

### What a state holds

| Type               | Contents                                                                        |
|--------------------|---------------------------------------------------------------------------------|
| `MatchState`       | the `Pitch`, the players in order, the `BallState`, the squad size per side, every player's perception memory, and each side's tactic |
| `PlayerMatchState` | `playerId`, `side`, `position`, `velocity`, `attributes`, `target`, `facing`    |
| `PlayerAttributes` | `maxSpeed` (m/s), `acceleration` (m/s²), `shotAccuracy`, `shotTechnique`, `weakFootAccuracy`, `keeperPositioning`, `keeperAnticipation`, `keeperReflexes`, `keeperHandling`, `jumping`, `heading` and `strength` (0 … 1) and `strongFoot`, fixed for the match |
| `BallState`        | `position`, `velocity`, `owner`, `lastTouch`, `held`, `touch`                   |
| `TeamSide`         | `kHome` or `kAway`                                                              |

Positions are meters in pitch coordinates, velocities are meters per second, in
the plane described by [match geometry](#match-geometry). The pitch
coordinate system is fixed, so `TeamSide` says which squad a player belongs to,
not by itself which way that squad attacks. The sandbox has no halves yet, so
home defends `x = 0` and attacks `+x` in every match; systems that need the
direction take it from `attackingDirection()`, the one place to change once
rules let teams switch ends.

`target` is where the player is moving to. It is empty until a command assigns
one, and without a target a player comes to a stop where he is; see
[player movement](players.md#player-movement). `attributes` default to `kDefaultMaxSpeed` (7.5 m/s) and
`kDefaultAcceleration` (4 m/s²), and `shotAccuracy` to `kDefaultShotAccuracy`
(0.5, an average finisher; see [shot decisions](on-ball.md#shot-decisions)). How he
strikes a shot is `shotTechnique` and `weakFootAccuracy`, both 0.5 by default,
and his `strongFoot`, the right ([shooting](on-ball.md#shooting)). How exactly a
goalkeeper takes up his place and how well he judges a ball played in behind
are `keeperPositioning` and `keeperAnticipation`, both 0.5 by default
([goalkeeper](goalkeeper.md#positioning-and-sweeping)); how quickly he reacts to a shot and how surely
he holds one are `keeperReflexes` and `keeperHandling`, both 0.5 by default,
and his anticipation is also how well he reads where a shot passes him
([shot stopping](goalkeeper.md#shot-stopping)). Only a goalkeeper uses them. How high a
player jumps, how well he times his jump and directs a header, and how well he
holds his own in a duel are `jumping`, `heading` and `strength`, all 0.5 by
default ([aerial duels](duels.md#aerial-duels-and-headers)); how closely his touches follow his
intent and how fast he runs with the ball, and how cleanly he takes it past an
opponent, are `ballControl` and `dribbling`, both 0.5 by default
([ball carrying](on-ball.md#ball-carrying)). They describe the
predefined test players of the sandbox, not a generated player.

The ball's `owner` is the player in control of it, empty while it is free, and
`lastTouch` the last player to kick or take it; see [possession](ball.md#possession).
`held` says the owner holds it in his hands: a goalkeeper who took it with them
([goalkeeper distribution](goalkeeper.md#distribution)). Only an owned ball
can be held; a new owner has it at his feet until the ball system puts it in
his hands. `touch` is the owner's touch while the ball rolls ahead of him
([ball carrying](on-ball.md#ball-carrying)): only an owned ball at the feet can be
touched on.
Every player has a slot for the action he has decided on and not yet done,
`pendingAction(index)`, empty for every player of a state created from a spec.
The action is a pass (see [passing](on-ball.md#passing)), a shot (see
[shot decisions](on-ball.md#shot-decisions)) or a touch to turn, carry or dribble (see
[ball carrying](on-ball.md#ball-carrying)), and only the player on the ball decides
one; `pendingPass()` finds the pass waiting to be played, if any.

`facing` is the unit vector a player looks along; it decides what he can see. It
is a vector rather than an angle so that no trigonometry, and none of its
platform differences, enters the state. The kickoff fixture turns each side
toward the goal it attacks.

Each side plays a [tactic](tactics.md#tactic-model) or none: `MatchState::create(spec,
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
[match phases](team-tactics.md#match-phases). Likewise `pitchControl()` caches the
[pitch-control](team-tactics.md#pitch-control) grid between refreshes, `chaser(side)` names the
player each side has sent after a free ball ([reception](ball.md#reception-interceptions-and-loose-balls)), and
`tactical(playerIndex)` holds each player's tactical runtime state, such as his
[desired region](team-tactics.md#desired-region), a goalkeeper's
[dive](goalkeeper.md#shot-stopping), when a player last [jumped](duels.md#aerial-duels-and-headers), the
[duel](duels.md#duels-on-the-ball) a defender fights and until when a player is off his feet
after one, and `press(side)` the side's
[press](team-tactics.md#pressing) in progress. `lastPass()`, `lastShot()` and
`lastReception()` record the last pass kicked, the last shot taken — with the
player it last came off, whether its outcome is recorded, and the keeper's
parry ([shot stopping](goalkeeper.md#shot-stopping)) — and the last
free ball controlled — with the teammate whose pass it was, if any —, and
`lastGoal()` the last goal scored. All are empty in a
state created from a spec.

`score()` is the goals both sides have scored. A spec states the score a match
starts at, 0:0 by default and never negative (`kInvalidScore`); the
[ball system](on-ball.md#goals) counts from there.

Every player also has a perception memory, `perception(playerIndex)`: what he
believes about the ball and the other players (see [perception](players.md#perception)).
A state created from a spec starts with every memory empty — perception is built
up by the match, not given — so the spec and the replay format need no field
for it; the state hash includes it.

The fields are deliberately few. Orientation, energy, action, perception, and
tactical runtime state from [implementation plan](implementation-plan.md)
section 6.2 arrive with the systems that fill them.

The ball carries its third dimension: `height` and `verticalVelocity` in meters
and meters per second, and `spin` as top- or backspin in radians per second
(section 6.7, [ball movement](ball.md#ball-movement)). A ball with neither height
nor vertical velocity lies or rolls on the grass. Validation rejects a
non-finite height, a ball below the ground, a vertical velocity or spin that is
not finite, and both of them beyond `kMaxBallSpeed` and `kMaxBallSpin`.

Player order is part of the state. Two states holding the same players in a
different order are not equal, because the order in which players are updated
has to stay fixed for a replay to reproduce.

### Construction is validation

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
| a held ball has an owner                          | `kHeldBallWithoutOwner`    |
| a held ball is in its side's goalkeeper's hands   | `kHeldBallByOutfielder`    |
| a ball touched on has an owner                    | `kTouchedBallWithoutOwner` |
| a ball touched on is not held, its seconds finite and not negative | `kInvalidCarryTouch` |

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

### Starting positions

A state a match starts from, such as the kickoff fixture below, must also have
everyone on the pitch. `checkStartingPositions(const MatchState&)` checks that
and returns one error per offender, players by index and then the ball, or an
empty list:

| Rule                              | Error code            |
|-----------------------------------|-----------------------|
| player positions are on the pitch | `kPlayerOutsidePitch` |
| the ball is on the pitch          | `kBallOutsidePitch`   |

### Error messages

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

### The seven-a-side kickoff fixture

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
rolling; see [ball movement](ball.md#ball-movement).
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

### What this is not

The state does not advance itself: the clock is the [match loop](#match-loop)'s,
and movement and ball physics are systems that run in it. There is no possession
and there are no rules here: no offside, no out of play, no fouls. `Pitch::contains()` decides whether
a position is on the rectangle, nothing more. A player standing on the goal
line is a valid state, and so is a ball behind it; whether that ball is a goal
is for the rules to decide.

## Match loop

`MatchSimulation` advances a match in fixed time steps. It lives in `sim-match`
(`matchSimulation.hpp`) and works on the [match state](#match-state); it
depends only on `sim-core`.

The loop owns time, update order and the rules every system follows. It does not
move anyone by itself: movement, ball physics, perception and decisions arrive as
systems that plug into it.

### One step, one tick

`step()` advances the match by exactly one tick. It takes no argument — no delta
time, no timestamp, no frame — so simulation time moves only through `step()`.
How often a caller steps changes how fast a match plays out on screen, never what
happens in it: stepping live, fast-forwarding or running headless gives the same
match.

The tick rate is a setting of `MatchSimulationSpec`, 30 Hz by default
(`kDefaultTicksPerSecond`). Elapsed time is `tick / ticksPerSecond`, one correctly
rounded division, so 300 ticks at 30 Hz are exactly 10 seconds.

| Accessor           | Meaning                                                     |
|--------------------|-------------------------------------------------------------|
| `tick()`           | ticks completed since the start                             |
| `elapsedSeconds()` | simulation time in seconds                                  |
| `state()`          | the state after the last step                               |
| `previousState()`  | the state before the last step; equal to `state()` at first |

`previousState()` and `state()` are the two snapshots a presentation layer
interpolates between to render at any frame rate.

### What happens in a step

A step from tick `t` to `t + 1`:

1. Applies the commands scheduled for tick `t`, in scheduling order.
2. Copies the resulting state into the next one.
3. Runs every system due at `t`, in the order the spec lists them. Each system
   reads the current state, commands included, and writes the next one.
4. After each system, checks the next state for non-finite values.
5. Advances the clock and makes the next state current.

After the clock advances, the events the step recorded are published: see
[match events](#match-events-and-decision-diagnostics).

### Commands

A command is an intent from outside the systems — a scenario script, a coach, a
test — that changes the state at a tick. `MatchCommand` is a `std::variant` of
the command types:

| Command             | Effect                                                                |
|---------------------|-----------------------------------------------------------------------|
| `MovePlayerCommand` | sets a player's movement target, moved onto the pitch if it lies off it |
| `GiveBallCommand`   | puts the ball at a player's feet; he owns it from this step on        |
| `PassCommand`       | asks a player to pass; played in this step if he owns the ball       |
| `ChangeTacticCommand` | switches a side to another tactic from this step on               |

`schedule(ScheduledCommand)` queues a command for the step that starts at its
tick. Commands of one tick apply in the order they were scheduled, which is the
explicit execution order a replay needs. `schedule()` rejects a tick before
`tick()`, an unknown player, a non-finite target and a tactic that does not fit
the squad, and leaves the queue unchanged; a command for `tick()` itself applies in the next step. Commands
known before kickoff go into `MatchSimulationSpec::commands`, which the
constructor schedules in order and rejects with `std::invalid_argument`.

`appliedCommands()` returns every command applied so far, in application order.
Together with the initial state, seed and tick rate this is the ordered command
log of the replay contract.

Commands are applied to a copy of the current state, so a step that fails leaves
`state()` and the command log as they were.

#### Tactic changes

A coach changes a tactic the way a script or a test does: with a
`ChangeTacticCommand` carrying the side and the new, already validated tactic.
There is no second path for AI or user input, so every change lands in the
ordered command log and a replay reproduces it, tactic content included. The
change applies at the start of its tick's step and records a `TacticChanged`
event with the tactic's name and content hash. The side's phase, press and
the players' tactical state are kept; each system follows the new tactic at its
next update, the tactical phase within a third of a second. A side that played
no tactic plays the new one from then on.

### Systems

A `MatchSystem` has a unique `name`, an `update` function, and a schedule:

```cpp
using MatchSystemUpdate = std::function<void(
    const MatchStepContext& context, const MatchState& current, MatchStateWriter& next)>;
```

**Read current, write next.** A system never sees what another system wrote in the
same step, and a player is never updated against positions other players already
moved to in that step. Update order is fixed, so replays reproduce, but it does
not change the result: the tenth player and the first see the same pitch. Each
field should have exactly one system that writes it.

**Write only what a match changes.** `MatchStateWriter` can set player and ball
positions and velocities, player targets and facings, perception memories, the
ball's owner and last touch, and the players' pending actions, and nothing else.
Squad, ids, sides, attributes, player order and the pitch have no setter, so a
system cannot break those invariants. Players
are addressed by their index in `MatchState::players()`.

**Keep no state of your own.** Everything that must survive a tick belongs in the
match state. A copy of a simulation continues exactly like the original only if
systems remember nothing between ticks.

**Schedule.** A system runs in the steps whose tick satisfies
`t % intervalTicks == phaseTicks`. The default, interval 1 and phase 0, runs it
every tick. At 30 Hz:

| Rate  | Interval | Example             |
|-------|----------|---------------------|
| 30 Hz | 1        | movement            |
| 10 Hz | 3        | perception          |
| 5 Hz  | 6        | decisions           |
| 2 Hz  | 15       | tactical evaluation |

Different phases spread systems of one rate over different ticks. In a step a
system skips, the fields it writes keep their values. Rates are whole numbers of
ticks, so the schedule stays exact over a whole match.

**Randomness.** `context.random(domain)` returns the simulation's stream for a
`RandomNumberGeneratorDomain`, derived from the spec's seed with
`deriveSeed()`. Systems draw in their fixed order, so the draws are part of what
a replay reproduces.

### The standard systems

`matchSetup.hpp` assembles the systems a real match runs. `MatchConfig` holds
every tunable parameter of those systems — the tick rate, ball physics and
perception, passing, reception, pursuit, decisions, phases, pitch control, positioning, off-ball and defensive decisions, duels, presses and restarts so far — and `makeMatchSystems(config)` returns them in their fixed order:

| Order | System          | Rate       | Writes                         |
|-------|-----------------|------------|--------------------------------|
| 1     | perception      | 10 Hz      | perception memories            |
| 2     | tactical phase  | 3 Hz       | team possession, phases of sides with a tactic |
| 3     | pitch control   | 3 Hz       | the pitch-control grid         |
| 4     | team pressing   | 5 Hz       | presses of sides with a tactic |
| 5     | tactical movement | 5 Hz     | movement targets, desired regions and actions -- off-ball and defensive -- of players of sides with a tactic, the goalkeepers' places ([goalkeeper](goalkeeper.md#positioning-and-sweeping)) |
| 6     | ball pursuit    | 10 Hz      | movement targets of the chasers, the chasers, the goalkeepers' judgements of a ball they could come for |
| 7     | pass decision   | 5 Hz       | the carrier's pending pass or shot ([shot decisions](on-ball.md#shot-decisions)) |
| 8     | duel            | 10 Hz      | the duel on the ball, in the defender's tactical state, and who is off his feet; the carrier's and the defender's movement targets, the ball's position as the carrier rolls it round him, its owner, velocity and last touch once it is won or loose; clears the loser's pending pass or shot ([duels](duels.md#duels-on-the-ball)) |
| 9     | shot stopping   | every tick | the dive of the keeper facing a shot, once he has reacted, in his tactical state ([shot stopping](goalkeeper.md#shot-stopping)) |
| 10    | player movement | every tick | player positions, velocities, facings -- toward the ball as the systems before it left it --; a keeper busy with a dive where it takes him, a player off his feet nowhere |
| 11    | ball movement   | every tick | ball position, velocity, owner, last touch, last pass and shot, the score and the last goal, a keeper's dive once the ball passed him; plays the owner's pending pass or shot and clears every other ([shooting](on-ball.md#shooting)) |
| 12    | restart         | every tick | ball owner, position, velocity and last touch after the ball went out or into a goal or a goalkeeper held it too long, and for a kickoff both sides' line-up; only with `restarts.enabled` ([restarts](#restarts)) |

`MatchSetup` is everything such a match starts from: initial state, config, seed
and commands. `startMatch(setup)` builds the simulation. A replay records a
setup (see [replay format](tooling.md#replay-format)); a parameter added to
`MatchConfig` must be added to the replay format too.

### Invariants and failures

The loop enforces the [match state](#match-state) invariants every tick. The
writer makes most of them impossible to break; what remains is finiteness, which
the loop checks after every system with `findNonFiniteValues()`.

Positions off the pitch are allowed. A ball over the touchline or a player behind
the goal line is football; what it means is for a rules system to decide.

A non-finite value or an exception from a system is a defect, not a match event.
The step stops at that system and the simulation stays at the last good state:
`state()`, `previousState()` and `tick()` are unchanged. `step()` returns a
`MatchStepError` naming the tick, the system and the broken values — for an
exception, the error is recorded and the exception propagates. Every later
`step()` returns the same error without running anything, and `hasFailed()`
reports the stopped simulation.

### Snapshots and replays

A `MatchSimulation` is copyable, and a copy includes state, clock and random
streams. The copy is a complete snapshot: stepping it and the original the same
number of times gives equal states. This is what rewinding and simulating ahead
of playback build on (see the [game design document](game-design-document.md),
section 8.10).

A replay is the same `MatchSimulationSpec` built again: initial state, seed, tick
rate and systems, with the applied commands as its command list — the ordered
command log of the replay contract in the
[implementation plan](implementation-plan.md), section 5.3.

### Performance

A successful step allocates nothing: the next state reuses its storage, the three
state buffers are swapped rather than rebuilt, and the finiteness check only
allocates when it has an error to report. Steps without a command skip the
commanded copy entirely; scheduling a command may allocate, applying it does not. A 90-minute match at 30 Hz is 162,000
steps, and the world simulation will run many of them headless.

### What this is not

The loop knows nothing about match length, halves, stoppage time or the end of a
match; a rules and match-clock system will. Simulation time is never scaled: a
shorter match ends earlier, it does not run faster.

## Match events and decision diagnostics

A match produces two kinds of output besides its state: **events**, immutable
facts about what happened, and **decision diagnostics**, the reasoning behind
what players decided. Both live in `sim-match` (`matchEvents.hpp`).

### Events

Systems record events through `MatchStepContext::record()` while they change the
state; commands that change possession record theirs before the systems run.
After a successful step, `MatchSimulation::events()` holds the events of that
step in the order they were recorded — the "publish" slot of the
[match loop](#match-loop). A failed step publishes nothing: `events()` keeps
the last good step's events.

Every event carries the `tick` of its step and the players involved:

| Event                | Fields                                                       | Recorded when                               |
|----------------------|--------------------------------------------------------------|---------------------------------------------|
| `PassAttempted`      | `passer`, `intendedReceiver`, `from`, `target`, `speed`      | a pass leaves the passer's foot             |
| `PassReceived`       | `receiver`, `passer`                                         | a teammate of the passer takes the pass     |
| `PassIntercepted`    | `interceptor`, `passer`, `position`                          | an opponent of the passer takes the pass, where the ball was |
| `LooseBallRecovered` | `player`, `position`                                         | someone takes a ball nobody played, or the passer takes his own pass back |
| `PossessionChanged`  | `previousOwner`, `newOwner`                                  | the ball's owner changes; either may be empty |
| `PhaseChanged`       | `side`, `previous`, `phase`                                  | a side with a tactic enters another [tactical phase](team-tactics.md#match-phases); `previous` is empty for its first |
| `BallWon`            | `winner`, `loser`, `position`, `cause`                       | an opponent wins the ball from its owner: `duel`, a presser in a [duel](duels.md#duels-on-the-ball), or `looseTouch`, he got to the ball rolling ahead of its owner after a touch ([ball carrying](on-ball.md#ball-carrying)); `PossessionChanged` follows |
| `PressingStarted`    | `side`, `carrier`, `trigger`, `assignments`                  | a side starts a [coordinated press](team-tactics.md#pressing); `trigger` is empty for a press of the pressing phase |
| `PressingEnded`      | `side`, `outcome`                                            | the press ends: `ballRegained`, `passedOut` or `carrierEscaped` |
| `PitchControlSampled` | `homeShare`, `ball`                                         | the [pitch control](team-tactics.md#pitch-control) grid is updated: home's share of the pitch and the ball's position, a regular sample for [analytics](tooling.md#match-analytics) |
| `RestartTaken`       | `kind`, `player`, `position`                                 | play [restarts](#restarts) after the ball went out or into a goal: `throwIn`, `goalKick`, `corner` or `kickoff`, to `player`; `position` is where the ball left the pitch, the centre spot for a kickoff. `PossessionChanged` follows |
| `TacticChanged`      | `side`, `tactic`, `contentHash`                              | a [tactic change command](#tactic-changes) applies; `tactic` is the new tactic's name |
| `ShotAttempted`      | `shooter`, `from`, `target`, `height`, `struckAt`, `struckHeight`, `speed`, `distance`, `opening` | a [shot](on-ball.md#shot-decisions) leaves the shooter's foot toward the aimed point and height; `struckAt` and `struckHeight` are the point and height it really left for, off by his [execution](on-ball.md#shooting) error; `distance` and `opening` are to and of the goal. `PossessionChanged` follows |
| `ShotDeflected`      | `shooter`, `shotTick`, `player`, `position`, `height`, `blocked` | a shot comes off an outfield player and flies on; `blocked` if it lost most of its speed |
| `ShotHitWoodwork`    | `shooter`, `shotTick`, `part`, `position`, `height`          | a shot rebounds from `postAtMinY`, `postAtMaxY` or the `crossbar` |
| `ShotResolved`       | `shooter`, `shotTick`, `outcome`                             | a shot has become what it will be, once per shot: `goal`, `saved`, `offTarget` or `blocked` |
| `GoalScored`         | `side`, `scorer`, `assist`, `ownGoal`, `score`               | the ball crosses a goal line inside the frame: a goal for `side`, by `scorer` — empty for a ball nobody touched —, with the `score` it makes |
| `SaveAttempted`      | `keeper`, `shooter`, `shotTick`, `result`, `position`, `height` | a shot passes the keeper of the goal it attacks and touches him, or was on its way in: `caught`, `parriedIntoPlay`, `parriedBehind` or `outOfReach` ([shot stopping](goalkeeper.md#shot-stopping)) |
| `AerialContest`      | `position`, `height`, `contestants`, `winner`, `play`        | players go up for a high ball where the first of them gets to it: each contestant's `player`, `reach` and whether he `reached` it, the `winner` — empty if nobody did — and his `play`: `shot`, `pass`, `knockDown`, `clearance`, `caught` or `punched` ([aerial duels](duels.md#aerial-duels-and-headers)). A header shot's `ShotAttempted` or a header pass's `PassAttempted` follows |
| `DribbleAttempted`   | `carrier`, `opponent`, `from`, `target`                      | the first touch of a [dribble](on-ball.md#dribble) past `opponent` leaves the carrier's foot toward `target` |
| `DribbleEnded`       | `carrier`, `opponent`, `outcome`, `position`                 | the dribble is over: `beaten`, he has the ball back farther up the pitch than the opponent, `kept`, he has it back short of him, or `lost`, to an opponent or out of play |
| `DuelStarted`        | `carrier`, `defender`, `position`                            | a presser takes on the carrier for the ball at his feet ([duels](duels.md#duels-on-the-ball)) |
| `DuelEnded`          | `carrier`, `defender`, `outcome`, `position`                 | the duel is over: `kept` by the carrier, `won` by the defender -- `BallWon` and `PossessionChanged` follow --, the ball knocked `loose` -- `PossessionChanged` to nobody follows --, or a `foul` |

The shot events name their shot by its `shooter` and `shotTick`, the tick of its
`ShotAttempted`; see [shooting](on-ball.md#shooting) and [shot stopping](goalkeeper.md#shot-stopping)
for what decides each of them.

`speed` is the speed the ball left the foot with, execution error included. A
pass shows up as

```text
PassAttempted(t, passer)  PossessionChanged(t, passer → none)
… PassReceived(t', receiver, passer)  PossessionChanged(t', none → receiver)
```

**Classification.** When a player takes a free ball, the ball's last touch
decides what it was. A last touch that kicked or headed the last pass
(`MatchState::lastPass()`) means the ball was passed: a teammate of the passer
received it, an opponent intercepted it. Without a last touch, after a shot, a
deflection or a keeper's parry of one, a headed clearance or a punch, or when
the passer takes his own ball back, the ball was loose.

Events are part of the match: the same setup produces the same events in the
same order. `addEvent()` feeds an event into a `StableHasher` — its type, tick
and every field — so replays can compare event sequences as well as states.
`eventName()` names an event for logs.

### Decision diagnostics

A `DecisionDiagnostic` explains one decision of a player on the ball (see
[pass decisions](on-ball.md#pass-decisions)):

| Field          | Meaning                                                     |
|----------------|-------------------------------------------------------------|
| `tick`         | the tick of the decision                                    |
| `player`       | the player on the ball                                      |
| `observations` | his perception memory at that moment                        |
| `candidates`   | every pass with all score components and rejection reason   |
| `shots`        | every zone of the goal he could aim at, the best first; only that one competed with the passes |
| `outcome`      | `kPassed`, `kShot`, `kHeld`, `kTurned`, `kCarried`, `kDribbled` or `kNoValidOption` |
| `chosen`       | index of the chosen pass in `candidates`, shot in `shots` or carry in `carries`, if he chose one |
| `scoring`, `shotScoring` | the weights the passes and shots were scored with, after the tactic's passing risk and a goalkeeper's directness; `passContributions()` and `shotContributions()` with them explain every utility |
| `carries`, `carryScoring` | his options with the ball at his feet -- holding, turning, carrying and dribbling -- with all score parts and rejection reason, and the weights they were scored with; `carryContributions()` with them explains every utility ([ball carrying](on-ball.md#ball-carrying)) |
| `held` | whether he held the ball in his hands: his passes throws, his lofted passes punts ([goalkeeper distribution](goalkeeper.md#distribution)) |
| `reason`, `target` | how he came to it -- `kChoice` among valid options, or looking for one `kScanFirstSide`, `kScanOtherSide`, `kForcedRejected` or `kForcedClear` -- and the point he looks at, clears the ball to or carries it to ([pass decisions](on-ball.md#without-a-valid-option)) |

An `ActionDiagnostic` explains one decision of a player without the ball (see
[off-ball movement](team-tactics.md#off-ball-movement)): the `tick`, the `player`, every
`candidates` action with its target, subject, weighted `scores` and utility, and
the index of the `chosen` one; `assigned` and `idlePress` mark an action he did
not choose, a role in his team's press or the press on a carrier who stands on
the ball ([pressing](team-tactics.md#pressing)). `MatchSimulation::actionDiagnostics()` holds
those of the last step.

A `SweepDiagnostic` explains a goalkeeper's call to come for a ball the
opponent played or to stay home (see [goalkeeper](goalkeeper.md#positioning-and-sweeping)): the `tick`,
the `player` and the `call` -- his and the first attacker's seconds to the
ball, his misjudgement, the threshold he judged it by and whether he comes. One
is reported when he first judges a ball and one when he turns back;
`MatchSimulation::sweepDiagnostics()` holds those of the last step.

Diagnostics are off by default. `MatchSimulation::setCollectDiagnostics(true)`
turns them on; `diagnostics()` then holds the diagnostics of the last step.
`setDiagnosticsFilter()` narrows them to some `players` and a tick range
`from`–`to` (both inclusive, either open): outside the range a step collects
nothing at all, and inside it the systems build diagnostics only for the
chosen players. Off, diagnostics cost one pointer check per decision.

Diagnostics are read-only output: the decision system builds them only on
request and only after it has chosen, so they cannot change a decision, and
building them draws no random numbers. They are never hashed and no system reads
them. A test runs the same match with and without diagnostics and requires
identical states and events at every tick.

### What this is not

There is no event history inside the simulation — each step publishes its own
events, and whoever needs a log keeps one. Match statistics are
[analytics](tooling.md#match-analytics) built on these events.

## Restarts

A ball that leaves the pitch stops on the line where it crossed it
([ball movement](ball.md#ball-movement)) — also one that went into a goal
([shooting](on-ball.md#shooting)). Without anything else, players then chase
it there and play on from the line. That is fine for short scenarios, but over
a whole match it blurs every statistic: a side can win the ball back by
walking to a ball it kicked out itself. The restart system settles who plays
on, simply and deterministically. It is **opt-in** (`MatchConfig::restarts`,
`enabled = false` by default), so matches recorded without it keep their
hashes; the [benchmark](tooling.md#sim-benchmark) turns it on.

### When

The ball is **out of play** (`isOutOfPlay()`) when it is free, at rest on the
ground and on a
touchline or goal line — exactly where a ball that left the pitch stops. At rest
means in every direction: a ball still in the air over the line, even one at the
apex of its flight with no speed across the grass, is in play. The
restart system runs every tick, last in the [standard order](#match-loop),
and restarts in the step after the ball stopped there.

While restarts are enabled, [ball movement](ball.md#ball-movement) leaves a ball
that is already out of play alone: a player standing within his control radius
of the line would otherwise receive or intercept it first — with the events
that come with that — only for the restart to hand it to the taker in the same
step.

### Who

`planRestart()` decides, without drawing a random number:

| Ball on         | Last touch by                | Restart     | Ball to                                   |
|-----------------|------------------------------|-------------|-------------------------------------------|
| the goal line it went in over | anyone         | `kickoff`   | the conceding side's player nearest the centre spot in its [line-up](#the-kickoff) |
| a touchline     | one side                     | `throwIn`   | the other side's player nearest the ball  |
| a touchline     | nobody                       | `throwIn`   | the nearest player of the side whose half it is |
| a goal line     | the side defending that line | `corner`    | the attacking side's player nearest the ball |
| a goal line     | the attackers, or nobody     | `goalKick`  | the defending side's goalkeeper, or its nearest player without one |

Home defends the goal line at `x = 0`, away the one at `x = length`. Ties in
distance go to the lower player index. A ball went in if nobody has touched it
since the state's last goal (`MatchState::lastGoal()`).

### Holding the ball too long

A goalkeeper may hold the ball in his hands for eight seconds (Law 12): the
referee counts the last five down. One who holds it longer than
`maxHoldSeconds` (8 s) since he took it -- his last touch -- concedes a
**corner**, at the corner of his goal line nearer the ball -- unless the
ball system is just playing the pass or shot he chose before the limit. The
other side's player nearest that corner, ties to the lower index, takes it: he
stands on the corner flag at rest, facing the centre of the pitch, the ball at
his feet. The system records `RestartTaken` (`corner`, the taker, the corner)
and `PossessionChanged` from the keeper to the taker. The keeper's own way out
is the [clearance](goalkeeper.md#the-ball-in-his-hands) two
seconds earlier; there is no visible count.

### What happens

The taker gets the ball at his feet, at rest, with the last touch his; one
who stumbled in a [duel](duels.md#off-his-feet) is up again. For a
throw-in and a corner he is not moved. A goal kick is taken from the goal
area: the ball lies on `goalKickSpot()`, the front edge of the goal area level
with where it went out but no wider than the goal area, and the taker stands
`carryDistance` behind it, at rest and facing up the pitch
([goalkeeper distribution](goalkeeper.md#the-goal-kick)). For a
kickoff both sides [line up](#the-kickoff) first. The system records `RestartTaken` (kind, taker, where the ball left
the pitch — the centre spot for a kickoff) and `PossessionChanged`, so [analytics](tooling.md#match-analytics) sees the
change of possession. From the next step on, play continues as after any other
change of possession: the taker decides, his team shapes up around him.

### The kickoff

A kickoff is taken the way the Laws ask for it. `lineUpForKickoff(state,
kicking, ball)` says where everybody stands:

- **Every player in his own half, in his side's starting formation.** The
  formation is the base shape of the side's [tactic](tactics.md#tactic-model), squeezed into
  its half: a slot at depth `d` stands at `d / 2` of the pitch length from its
  own goal line, at the slot's width. The reference tactic's striker, at depth
  0.65, waits 19.5 m from his goal line on the 60 m sandbox pitch. A scripted
  side has no formation: its players stay where they are, and one beyond the
  halfway line goes straight back onto it.
- **The opponents outside the centre circle.** An opponent of the side kicking
  off who would stand inside it steps straight back, toward his own goal line,
  onto the circle.
- **The ball on the centre spot, the taker behind it.** The taker is the player
  of the side kicking off who is nearest to the centre spot in that formation —
  the striker of every preset — with ties going to the lower player index. He
  stands `carryDistance` behind the spot, in his own half, so the ball at his
  feet lies on it; on a pitch shorter than that, on his goal line.

**How they get there.** They are placed, in the one step that takes the
kickoff: nobody walks back, and no tick is spent waiting for anybody. Both
sides then start as at the beginning of a match — at rest, facing the goal they
attack, without movement targets, pending actions, [memories](players.md#perception),
desired regions, presses, chasers or [pitch control](team-tactics.md#pitch-control), which
its next refresh computes anew — so nothing of the play before the goal
carries over the halfway line. **The match clock** runs on as through every
other restart: a goal costs the step the ball lies in the goal and nothing
more. There is no stoppage and no added time yet; when the match gets halves
and a whistle, the time a kickoff takes is theirs to decide.

**The same kickoff starts a match.** The [tactic match](scenarios.md#the-tactic-match)
begins in this line-up with home kicking off, whether restarts are enabled or
not; only the restart after a goal needs them. Its opening kickoff is a
`GiveBallCommand` and records no `RestartTaken`.

### What this is not

No throw-in technique, no set pieces: the ball is simply handed to the right
side. For a throw-in, a goal kick and a corner, players other than the goal
kick's and a holding corner's taker do not take up positions; they are wherever the ball's journey
left them, opponents in the penalty area at a goal kick included. The kickoff is not
kicked to a teammate and need not go forward: the taker has the ball and
decides like any other carrier, and the opponents may enter the circle at once.
