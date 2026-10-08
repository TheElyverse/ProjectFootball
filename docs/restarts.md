# Restarts

A ball that leaves the pitch stops on the line where it crossed it
([ball movement](ball-movement.md)) — also one that went into a goal
([shooting](shooting.md)). Without anything else, players then chase
it there and play on from the line. That is fine for short scenarios, but over
a whole match it blurs every statistic: a side can win the ball back by
walking to a ball it kicked out itself. The restart system settles who plays
on, simply and deterministically. It is **opt-in** (`MatchConfig::restarts`,
`enabled = false` by default), so matches recorded without it keep their
hashes; the [benchmark](sim-benchmark.md) turns it on.

## When

The ball is **out of play** (`isOutOfPlay()`) when it is free, at rest on the
ground and on a
touchline or goal line — exactly where a ball that left the pitch stops. At rest
means in every direction: a ball still in the air over the line, even one at the
apex of its flight with no speed across the grass, is in play. The
restart system runs every tick, last in the [standard order](match-loop.md),
and restarts in the step after the ball stopped there.

While restarts are enabled, [ball movement](ball-movement.md) leaves a ball
that is already out of play alone: a player standing within his control radius
of the line would otherwise receive or intercept it first — with the events
that come with that — only for the restart to hand it to the taker in the same
step.

## Who

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

## What happens

The taker gets the ball at his feet, at rest, with the last touch his. For a
throw-in and a corner he is not moved. A goal kick is taken from the goal
area: the ball lies on `goalKickSpot()`, the front edge of the goal area level
with where it went out but no wider than the goal area, and the taker stands
`carryDistance` behind it, at rest and facing up the pitch
([goalkeeper distribution](goalkeeper-distribution.md#the-goal-kick)). For a
kickoff both sides [line up](#the-kickoff) first. The system records `RestartTaken` (kind, taker, where the ball left
the pitch — the centre spot for a kickoff) and `PossessionChanged`, so [analytics](match-analytics.md) sees the
change of possession. From the next step on, play continues as after any other
change of possession: the taker decides, his team shapes up around him.

## The kickoff

A kickoff is taken the way the Laws ask for it. `lineUpForKickoff(state,
kicking, ball)` says where everybody stands:

- **Every player in his own half, in his side's starting formation.** The
  formation is the base shape of the side's [tactic](tactics.md), squeezed into
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
attack, without movement targets, pending actions, [memories](perception.md),
desired regions, presses, chasers or [pitch control](pitch-control.md), which
its next refresh computes anew — so nothing of the play before the goal
carries over the halfway line. **The match clock** runs on as through every
other restart: a goal costs the step the ball lies in the goal and nothing
more. There is no stoppage and no added time yet; when the match gets halves
and a whistle, the time a kickoff takes is theirs to decide.

**The same kickoff starts a match.** The [tactic match](scenarios.md#the-tactic-match)
begins in this line-up with home kicking off, whether restarts are enabled or
not; only the restart after a goal needs them. Its opening kickoff is a
`GiveBall` command and records no `RestartTaken`.

## What this is not

No throw-in technique, no set pieces: the ball is simply handed to the right
side. For a throw-in, a goal kick and a corner, players other than the goal
kick's taker do not take up positions; they are wherever the ball's journey
left them, opponents in the penalty area at a goal kick included. The kickoff is not
kicked to a teammate and need not go forward: the taker has the ball and
decides like any other carrier, and the opponents may enter the circle at once.
