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
| the goal line it went in over | anyone         | `kickoff`   | the conceding side's player nearest the centre spot |
| a touchline     | one side                     | `throwIn`   | the other side's player nearest the ball  |
| a touchline     | nobody                       | `throwIn`   | the nearest player of the side whose half it is |
| a goal line     | the side defending that line | `corner`    | the attacking side's player nearest the ball |
| a goal line     | the attackers, or nobody     | `goalKick`  | the defending side's goalkeeper, or its nearest player without one |

Home defends the goal line at `x = 0`, away the one at `x = length`. Ties in
distance go to the lower player index. A ball went in if nobody has touched it
since the state's last goal (`MatchState::lastGoal()`).

## What happens

The taker gets the ball at his feet, at rest, with the last touch his; he is
not moved. The system records `RestartTaken` (kind, taker, where the ball left
the pitch — the centre spot for a kickoff) and `PossessionChanged`, so [analytics](match-analytics.md) sees the
change of possession. From the next step on, play continues as after any other
change of possession: the taker decides, his team shapes up around him.

## What this is not

No throw-in technique, no set pieces: the ball is simply handed to the right
side. Players do not take up positions for the restart; they are wherever the
ball's journey left them — also for a kickoff, which is taken from where the
taker stands, not from the centre spot, with both sides anywhere on the pitch.
