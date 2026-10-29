# Set pieces

A throw-in, a corner, a goal kick and a kickoff are set pieces: play stops, the
players take up their places, and the taker plays the ball as his side's plan
says. The plans are part of the [tactic](tactics.md#tactic-model)
(`setPiecePlan.hpp` in `sim-tactics`); the [restart system](match-engine.md#restarts)
awards a set piece, and the set piece system (`setPieces.hpp` in `sim-match`,
sixth of the [standard systems](match-engine.md#the-standard-systems)) sets it
up and takes it. Free kicks and penalties are [their own](fouls.md).

## Plans

`SetPiecePlans` hold a `SetPiecePlan` for each kind — `kickoff`, `throwIn`,
`corner`, `goalKick`:

| Field      | Meaning |
|------------|---------|
| `taker`    | the slot whose player takes it; empty: the restart's choice — the player nearest the ball, the goalkeeper for a goal kick |
| `delivery` | what the pass aims at: `short`, `long`, and for a corner `nearPost`, `farPost` or `penaltySpot` instead of `long` |
| `runners`  | the slots that go forward to receive it when the side takes it — into the box at a corner, short at a throw-in or a goal kick —, in order |
| `markers`  | the slots that mark the opponent's runners man for man when the opponent takes it, in order |

Empty lists leave the choice to the match (below). The defaults are a short
kickoff, throw-in and goal kick and a corner to the far post. Tactic files
hold them in an optional `setPieces` block (tactic format version 5); a file
without one plays the defaults. `Tactic::create()` rejects a slot that does not
exist, one named twice in a list, and a delivery the kind cannot have.

## Positions

While the set piece is set up, `setPieceTargets()` gives everyone with a place
in it his movement target, over the tactical movement's; everyone else plays
his [desired region](team-tactics.md#desired-region). The taker goes behind the
ball.

| Kind | The side taking it | The other side |
|------|--------------------|----------------|
| corner | its runners — by default the four outfield players whose slots stand highest — to the delivery's place first, then the near post, the far post, the penalty spot, the goal area's front and the edge of the area, each 2 m out from the goal line where it is at a post; for a `short` corner the first runner to a short option eight metres along the goal line | its markers — by default its outfield players whose slots stand deepest, as many as there are runners — a metre goal side of the places the runners take; the next two in zones at the near post and the goal area's front; the keeper on his line |
| throw-in | its runners — by default the two outfield players nearest the ball — eight metres up the line and ten metres infield | its markers — by default those nearest the ball — goal side of those places |
| goal kick | its runners — by default the two outfield players whose slots stand deepest — to the front of the penalty area, at its sides | everyone in the penalty area out of it |
| kickoff | the [line-up](match-engine.md#the-kickoff) | the line-up |

At a corner and a throw-in the other side keeps a centre circle's radius from
the ball.

## The delivery

The taker plays once the kind's setup time has passed — `throwInSeconds` (3 s),
`cornerSeconds` (8 s), `goalKickSeconds` (6 s), `kickoffSeconds` (0 s) — he
stands within `readyRadius` (0.75 m) of his place behind the ball, and everyone
else with a place stands within `placeRadius` (2 m) of it, or `waitSeconds`
(10 s) more have passed. The ball lies dead until then: no player receives,
chases or wins it, and no restart is taken.

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

`SetPieceConfig` holds every number above and is part of `MatchConfig` and of
every replay.

## Scenarios

`corner` and `throw-in` play the eleven-a-side reference against itself from
the kickoff line-up, the ball at rest on away's goal line by the corner flag or
on the touchline at two thirds of the length, last touched by away;
`goal-kick` is the sandbox's ([scenario catalogue](scenarios.md#scenario-catalogue)).
`tests/acceptance/setPieceScenarioTests.cpp` checks over three seeds that the
ball stays dead until the taker plays it, after the setup time; at the corner
that at least three of home's players are in the box, four runners and five
defenders in their places, nobody of away within the distance of the ball, and
the far-post delivery meant for a runner; at the throw-in a short pass to an
option near the thrower, both options and their markers in place; and at the
goal kick that nobody of home is left in the area when it is taken.

## What this is not

- The taker plays the plan, not a decision: the delivery does not weigh its
  options, and a marked runner gets the ball all the same.
- Nobody blocks or holds at a corner, the keeper does not come for it before
  it is played, and a throw-in is a pass along the ground or through the air,
  not a throw.
- The opening kickoff of a fixture is a `GiveBallCommand`: its taker decides
  like any carrier.
- At about one throw-in and less than one corner a side per six-minute match,
  the benchmark's matches leave the pitch less than real ones.
