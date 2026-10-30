# Set pieces

A throw-in, a corner, a goal kick, a kickoff, a free kick and a penalty are set
pieces: play stops, the
players take up their places, and the taker plays the ball as his side's plan
says. The plans are part of the [tactic](tactics.md#tactic-model)
(`setPiecePlan.hpp` in `sim-tactics`); the [restart system](match-engine.md#restarts)
awards a set piece, and the set piece system (`setPieces.hpp` in `sim-match`,
sixth of the [standard systems](match-engine.md#the-standard-systems)) sets it
up and takes it. [Fouls](fouls.md) and [offside](match-engine.md#offside) award
free kicks and penalties.

## Plans

`SetPiecePlans` hold a `SetPiecePlan` for each kind — `kickoff`, `throwIn`,
`corner`, `goalKick`, `freeKick`, `penalty`:

| Field      | Meaning |
|------------|---------|
| `taker`    | the slot whose player takes it; empty: the restart's choice — the player nearest the ball, the goalkeeper for a goal kick |
| `delivery` | what the pass aims at: `short`, `long`, and for a corner `nearPost`, `farPost` or `penaltySpot` instead of `long` |
| `runners`  | the slots that go forward to receive it when the side takes it — into the box at a corner, short at a throw-in or a goal kick —, in order |
| `markers`  | the slots that mark the opponent's runners man for man when the opponent takes it, in order |

Empty lists leave the choice to the match (below), as does a list whose
players are all sent off or the taker. The defaults are a short kickoff,
throw-in and goal kick and a corner to the far post; a free kick's and a
penalty's delivery is the taker's own decision (below), so of a free kick's
plan only the taker, runners and markers are read, of a penalty's only the
taker. Tactic files hold them in
an optional `setPieces` block, each kind optional, which needs no new format version
([versioning](tactics.md#versioning)); a file without one plays the defaults.
`Tactic::create()` rejects a slot that does not exist, one named twice in a
list, runners or markers for a kickoff, which lines up instead, and a delivery
the kind cannot have.

## Positions

While the set piece is set up, `setPieceTargets()` gives everyone with a place
in it his movement target, over the tactical movement's; everyone else plays
his [desired region](team-tactics.md#desired-region). The taker goes behind the
ball.

| Kind | The side taking it | The other side |
|------|--------------------|----------------|
| corner | its runners — by default the four outfield players whose slots stand highest — to the delivery's place first, then the near post, the far post, the penalty spot, the goal area's front and the edge of the area, each 2 m out from the goal line where it is at a post; for a `short` corner the first runner to a short option eight metres along the goal line | its markers — by default its outfield players whose slots stand deepest, as many as there are runners — a metre goal side of the places the runners take; the next two in zones at the near post and the goal area's front; the keeper on his line |
| throw-in | its runners — by default the two outfield players whose slots stand nearest the ball — eight metres up the line and ten metres infield | its markers — by default those whose slots stand nearest the ball — goal side of those places |
| goal kick | its runners — by default the two outfield players whose slots stand deepest — to the front of the penalty area, at its sides | everyone in the penalty area out of it |
| kickoff | the [line-up](match-engine.md#the-kickoff) | the line-up |

The other side keeps its distance from the ball (`keptDistance()`):
`throwInDistance` (2 m) at a throw-in (Law 15), a centre circle's radius at a
corner or a free kick (Laws 17, 13). Whoever would stand nearer, at his place or
where he is without one, goes out to half a metre beyond it, and the taker
plays only once nobody of the other side stands nearer — or `waitSeconds` more
have passed. Choosing
by slots rather than by where the players stand keeps the choice while they
move.

While a set piece is set up its side is the side on the ball: both teams play
the [match phase](team-tactics.md#match-phases) that follows from it.

## The delivery

The taker plays once the kind's setup time has passed — `throwInSeconds` (8 s),
`cornerSeconds` (20 s), `goalKickSeconds` (15 s), `kickoffSeconds` (0 s) — he
stands within `readyRadius` (0.75 m) of his place behind the ball, and everyone
else with a place stands within `placeRadius` (2 m) of it, or `waitSeconds`
(10 s) more have passed. The ball lies dead until then: no player receives,
chases or wins it, and no restart is taken. The setup times are real
football's, give or take — fetching the ball, lining up, the referee's
whistle —: with them the ball is dead a fifth of the match rather than a
twentieth.

`plannedDelivery()` is the pass:

- a corner aimed at an area goes lofted, `flightSeconds` (1.6 s) in the air, to
  that place, meant for the runner who stands there; a short one goes along
  the ground to the short option;
- a throw-in, a goal kick or a kickoff `short` goes along the ground to the
  first runner — for a kickoff the nearest teammate —; `long` goes lofted to
  the side's most advanced outfield player.

The taker gets the ball at his feet with the pass pending; the ball system plays
it in the same step. **`SetPieceTaken`** records the kind, the side, the taker,
the planned delivery, the pass's target and its receiver. Nobody is offside
from a throw-in, a corner or a goal kick ([offside](match-engine.md#offside)).
The [statistics](tooling.md#match-analytics) count each side's corners,
throw-ins and goal kicks.

Offside and fouls award free kicks and penalties without restarts too, so the
set piece system runs whenever restarts, offside or fouls are on.

`SetPieceConfig` holds every number above and is part of `MatchConfig` and of
every replay. `tests/unit/sim-match/setPiecesTests.cpp` checks the distances,
the planned taker and deliveries, the side on the ball, and that a kickoff ends
a set piece being set up.

## Free kicks

A direct free kick follows a foul outside the offender's penalty area, an
indirect one an offside ([fouls](fouls.md#the-decision),
[offside](match-engine.md#the-free-kick)). Both are set up for
`freeKickSeconds` (20 s). One no farther than `freeKickRange` (32 m) from the
centre of the goal it attacks goes for goal:

- **The wall.** The defending side's outfield players whose slots stand highest
  form it across the way from the ball to the goal's centre, half a metre beyond
  a centre circle's radius from the ball, `wallSpacing` (0.6 m) apart:
  `wallPlayers` (4) within 22 m of the goal, one fewer within 27 m, two fewer
  beyond. The keeper keeps his own [positioning](goalkeeper.md#positioning-and-sweeping)
  for the ball.
- **The box.** The attacking side's runners go into the box as at a corner, to
  the penalty spot first; the defenders' markers go goal side of them, and
  everyone else of the defending side keeps a centre circle's radius from the
  ball.

Farther out nobody takes a place but the taker, and the other side keeps its
distance. The taker gets the ball at his feet with nothing pending: he decides
like any carrier ([decision making](on-ball.md#decision-making)) — a shot, a
cross, a pass short or long, or a carry —, after his `minHoldSeconds`.

**Indirect.** No goal comes of an indirect free kick until another player has
touched the ball (Law 13): `MatchState::indirectKick()` holds the taker's touch
until then, his shot options are empty, and a ball he plays into the goal is
out over the goal line instead — into the opponents' goal a goal kick, into
his own a corner. A free kick is not exempt from
offside.

## Penalties

A foul in the offender's penalty area gives a penalty from the penalty spot,
set up for `penaltySeconds` (10 s). Everyone but the taker and the defending
keeper stands outside the penalty area and a centre circle's radius from the
spot — by `placeRadius` and half a metre more, so that one near enough to his
place to count as in it is out —, level with where he stood; the keeper on his
line between the posts. The taker shoots: the [shot candidates](on-ball.md#shot-decisions)
from the spot, chosen by their utility at the decision temperature from the
`kAi` stream, as any shot decision is. The shot is **placed**
(`ShotIntent::placed`): struck from a still ball in his own time, it errs
`placedErrorShare` (0.5) of what the same strike in open play would
([the strike](on-ball.md#the-strike)). Otherwise it is an ordinary shot: the keeper
[saves](goalkeeper.md#shot-stopping) it or not, and a parry into play is live —
whoever gets to it first plays the rebound.

`SetPieceTaken` records the kind, the taker and, for a penalty, the shot's
target; `ShotAttempted`, `ShotResolved` and `GoalScored` record the outcome.

## Scenarios

`corner` and `throw-in` play the eleven-a-side reference against itself from
the kickoff line-up, the ball at rest on away's goal line by the corner flag or
on the touchline at 65 % of the length, last touched by away;
`goal-kick` is the sandbox's ([scenario catalogue](scenarios.md#scenario-catalogue)).
`tests/acceptance/setPieceScenarioTests.cpp` checks over three seeds that the
ball stays dead until the taker plays it, after the setup time; at the corner
that at least three of home's players are in the box, four runners and five
defenders in their places, nobody of away within the distance of the ball, and
the far-post delivery meant for a runner; at the throw-in a short pass to an
option near the thrower, both options and their markers in place; and at the
goal kick that nobody of home is left in the area when it is taken.

`tests/acceptance/freeKickScenarioTests.cpp` awards home a free kick or penalty
in the eleven-a-side reference at tick 0. Over thirty seeds a direct free kick
22 m out faces a wall of at least three at the distance, and the wall blocks
some of the taker's shots while others clear it; over forty seeds a penalty is
taken with only the taker and the keeper in the area, between half and nine in
ten are scored — 24 of the 40, three in five, at the time of writing; real
football scores about three in four —, and at least one
is parried into play and played again. An indirect free kick struck straight in
gives a goal kick, the same ball without the indirect kick a goal.

## What this is not

- The taker plays the plan, not a decision: the delivery does not weigh its
  options, and a marked runner gets the ball all the same. A goal kick follows
  the plan, not the keeper's [directness](goalkeeper.md#distribution).
- Every lofted delivery takes `flightSeconds`, whatever its length, up to the
  fastest lofted pass: a long goal kick or throw-in may fall short.
- Nobody blocks or holds at a corner, the keeper does not come for it before
  it is played, and a throw-in is a pass along the ground or through the air,
  not a throw.
- The wall does not jump, and no attacker stands in it; the keeper does not
  line it up. A free kick taker has no technique of his own beyond his shot and
  pass; a penalty taker does not feint, and the keeper does not move early.
- The taker may play the ball twice: carry a free kick, or play his own penalty
  again off the woodwork (Laws 13, 14 forbid it). A direct free kick played
  straight into his own goal counts, where Law 13 gives a corner. At a penalty
  the others are not kept behind the penalty mark.
- A penalty taker without a valid shot from the spot (a `maxShotDistance`
  below 11 m) plays on as any carrier. A plan's free-kick taker takes every
  free kick, wherever it is.
- The opening kickoff of a fixture is a `GiveBallCommand`: its taker decides
  like any carrier. The second half's kickoff is a set piece when the match
  takes restarts.
- At about one throw-in and less than one corner a side per six-minute match,
  the benchmark's matches leave the pitch less than real ones.
