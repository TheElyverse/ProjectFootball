# Aerial duels and headers

A ball in the air belongs to whoever gets highest to it first. When a high
ball comes to a player, everyone near enough goes up for it; their jumps are
timed and reach so high, and of those who get to it the duel decides who wins
it. The winner heads it — at goal, to a teammate, down to one close by, or
clear — and a goalkeeper in his area catches it or punches it clear
(implementation plan §6.1, stage M6; GDD §8.8). The model lives in
`sim-match` (`aerialDuels.hpp`), and the [ball system](ball-movement.md)
plays it out where the ball meets the players.

```text
high ball → first contact: findAerialContact()
          → who goes up: findChallengers()
          → jumps: timing, reach (kExecution)
          → duel: duelUtility(), chooseByUtility() (kExecution)
          → the winner's touch: decideHeader() (kAi), executeHeader() (kExecution), or hold / punch
```

## In the air or on the ground

The ball's height decides whether a contest is aerial at all. A ball no higher
than `controlHeight` (1 m) where a player gets to it is taken at his feet as
it always was ([reception](reception.md)): the first to reach it has it. The
same delivery played above that is contested in the air.

`findAerialContact()` finds the first player to get to a high ball on its way
in a tick, with the same search reception uses (`findFirstReach()`), each
player reaching it as `aerialReach()` says:

- **with his head**, within `headRadius` (0.7 m) of him along the ground, up to
  `headHeight` (1.9 m, the ball's centre as he meets it standing) plus his
  jump;
- **with his hands**, if he is the goalkeeper and he and the ball are in his
  own penalty area where his hands first get to it (`hasHands()`): within
  reception's `handsRadius` (1.2 m), up to its `handsHeight` (2.2 m) plus his
  jump;
- and up to `attemptMargin` (0.3 m) beyond that: a player goes up for a ball a
  little too high for him, and does not get to it.

A ball that a player gets to in the air at the same moment as, or before,
anyone takes one at his feet, is contested. A ball that never rises above
`controlHeight` in the tick — on the grass, or coming down from no higher —
is not searched at all (`mayRiseAbove()`). A keeper's hands used to take every
ball up to `handsHeight` in his area; a ball above `controlHeight` there is
now contested like any other, and uncontested he still holds it.

A player who went up for a ball is **in the air** for `landingSeconds` (0.6 s)
after (`isInTheAir()`, the `lastJump` of his tactical state): he goes up for
no other ball until he has landed, so a ball nobody reached is not contested
again by the same players as it flies on, nor taken at their feet in the same
tick. It flies on past them to the keeper who faces it and to whoever else
gets to it first. Like reception, the contest leaves
out a keeper busy with a dive or facing a shot ([shot stopping](shot-stopping.md))
and the ball's last touch within `reclaimDelaySeconds` (`mayCompete()`). A shot
still fast enough to come off a body is no aerial ball: it is
[deflected](shooting.md#on-the-way).

## Who goes up

`findChallengers()`: the first player, and every other player who is within
`contestRadius` (1.5 m) of the ball at that moment and whose `aerialReach()`
gets as high as the ball is there, in player order. For each the duel knows

- whether he has his hands: `hasHands()` where he and the ball are at that
  moment;
- how high he stands — `headHeight`, or `handsHeight` with his hands — and how
  high his jump lifts him: `lowestJump` (0.3 m) at `jumping` 0 to
  `highestJump` (0.7 m) at 1 (`jumpRise()`);
- how late he is: how far beyond his own reach of it he still is from the
  ball, at his `maxSpeed` — none for the first;
- his body position: how square he stands to it, from 0 with his back to the
  ball's way to 1 facing it.

## The jump

Every challenger aims the top of his jump at the ball's arrival and misses it
by a triangular draw spread `timingError` (0.25 s) times `skillErrorFactor()`
of his `heading` — a keeper with his hands of his `keeperHandling` — two draws
from the `kExecution` stream each, in player order (`timingSpread()`). His jump
is a parabola under gravity, and off its top he is that much lower:

```text
reach = standing + max(0, rise − ½ · gravity · offset²)
```

(`reachAt()`). He reaches the ball if its centre is no higher than that. A ball
below his head he reaches however he timed his jump, but a mistimed jump
still spoils the header. His **mistiming** is how far off he was, as a share of
`timingError`, at most 1.

## The duel

Of those who reached the ball, `chooseByUtility()` picks the winner at
`duelTemperature` (0.3) — one draw from `kExecution`, the best the most likely,
never a certainty — by `duelUtility()`:

```text
utility = reachWeight · min(reach − ball height, reachCap)
        − arrivalWeight · late
        + strengthWeight · strength
        + bodyWeight · body
        + keeperAdvantage · keeperHandling        (a keeper with his hands)
```

with `reachWeight` 4 per meter up to `reachCap` (0.3 m), `arrivalWeight` 5 per
second, and `strengthWeight`, `bodyWeight` and `keeperAdvantage` 1. So arrival
time, reach, jump, strength and body position all decide, and a player who gets
there 0.1 s late gives away half a point — as much as half the gap between the
weakest and the strongest player.

Nobody reaching the ball, it flies on as if they were not there.

## The keeper's hands

A keeper who wins a ball with his hands holds it with `holdChance()`:
certainly if no opponent went up with him; otherwise all but `contestedDrop`
(0.3) of the time for a keeper of average handling, the share scaled by
`skillErrorFactor()` of his `keeperHandling`. One draw from `kExecution`,
always. Held, he has the ball like any player who takes a free ball. Dropped,
he punches it: it leaves his fists as a clearance would leave a head, his
errors scaled by his handling rather than heading.

So the keeper's handling is his advantage three times over: his hands reach
higher than a head and from farther away, it counts in the duel, and it decides
whether he holds what he wins.

## The header

The winner decides his header like every decision, from options with
utilities chosen by `chooseByUtility()` — at `decisionTemperature` (0.15), one
draw from the `kAi` stream (`decideHeader()`). A header is played without a
second look, so `headerOptions()` reads the true state rather than his
[perception](perception.md):

| Play        | When                                          | Aim                                                                                                                                               | Utility                                                           |
|-------------|-----------------------------------------------|---------------------------------------------------------------------------------------------------------------------------------------------------|-------------------------------------------------------------------|
| `clearance` | always                                        | `clearanceDistance` (30 m) up the pitch, on the ground, at `maxHeaderSpeed` (18 m/s)                                                              | 1 − distance to his own goal / `clearanceZone` (35 m), at least 0 |
| `shot`      | the goal he attacks within `shotRange` (14 m) | `shotInside` (0.5 m) inside the post away from the opposing keeper — the centre without one —, `shotHeight` (0.5 m) high, at `shotSpeed` (14 m/s) | 1 − distance to the goal / `shotRange`                            |
| `knockDown` | a teammate within `knockDownRange` (6 m)      | down to his feet, there in `passSeconds` (0.8 s) along the ground                                                                                 | `passWeight` (0.6) · open · (1 − distance / `passRange`)          |
| `pass`      | a teammate within `passRange` (20 m)          | the same                                                                                                                                          | the same                                                          |

A teammate is open as far as his nearest opponent is from him, fully at
`openDistance` (5 m).

`executeHeader()` carries it out from the ball where it meets the head: the
ball leaves toward the aimed point on the flight that brings it to the aimed
height there (`launchVerticalVelocity()` from the ball's own height, so a
header down is a launch below it), strayed across and up by a triangular
error of `directionError` (0.06 m per meter to the aim) and in pace by a
uniform one of `speedError` (10 %) — five draws from `kExecution`, always.
The flight is planned at the intended speed, at most `maxHeaderSpeed`; one
the drag stops short of goes up at 45 degrees. The pace error scales it along
the ground and up alike, and a header never leaves faster than
`maxHeaderSpeed` in all.
Both are scaled by `headerErrorFactor()`: `skillErrorFactor()` of his
heading, times `1 + mistimedErrorFactor · mistiming` — a fully mistimed header
is three times as wide. The ball leaves without spin.

What the ball is afterwards follows the play:

- a **shot** is a new [shot](shooting.md): `ShotAttempted`, the keeper faces
  it, and it ends in its `ShotResolved`;
- a **pass** or **knock-down** is a pass: `PassAttempted`, and a teammate who
  takes it received it, an opponent intercepted it;
- a **clearance** or a **punch** is no pass: whoever takes it recovers a loose
  ball.

A shot still open when the ball was headed or punched has become what it will
be, as if the player had taken it ([shooting](shooting.md#outcomes)). The ball
flies on from the head for the rest of the tick: the keeper who faces a
header shot meets it where it passes him, and it comes off the goal frame,
but nobody else gets to it before the next tick.

## Events

`AerialContest` records every contest, with where the ball was and how high
when the first player got to it, every `contestants` — the player, the
`reach` his jump met it with and whether he `reached` it —, the `winner`, empty
if nobody reached it, and his `play`: `shot`, `pass`, `knockDown`,
`clearance`, `caught` or `punched` ([match events](match-events.md)). The
events of the play follow it.

## Configuration

`AerialConfig` is the `aerial` part of `MatchConfig` and of every replay:

| Field                 | Default | Meaning                                                         |
|-----------------------|---------|-----------------------------------------------------------------|
| `headHeight`          | 1.9 m   | the ball's centre as a standing player meets it with his head   |
| `headRadius`          | 0.7 m   | how close along the ground it must come to his head             |
| `lowestJump`          | 0.3 m   | how high a jump lifts a player of jumping 0                     |
| `highestJump`         | 0.7 m   | and one of jumping 1                                            |
| `timingError`         | 0.25 s  | how far an average header times his jump off the ball's arrival |
| `attemptMargin`       | 0.3 m   | he goes up for a ball this far above the best he can reach      |
| `contestRadius`       | 1.5 m   | who else goes up when the first player gets to the ball         |
| `landingSeconds`      | 0.6 s   | after going up, he goes up for nothing else this long           |
| `reachWeight`         | 4 per m | what getting above the ball is worth in the duel                |
| `reachCap`            | 0.3 m   | up to this far above it                                         |
| `arrivalWeight`       | 5 per s | what getting there late costs                                   |
| `strengthWeight`      | 1       | what strength is worth                                          |
| `bodyWeight`          | 1       | what facing the ball is worth                                   |
| `keeperAdvantage`     | 1       | what a keeper's handling is worth, with his hands               |
| `duelTemperature`     | 0.3     | how surely the best in the duel wins                            |
| `contestedDrop`       | 0.3     | the share of contested balls an average keeper drops            |
| `shotRange`           | 14 m    | the farthest from the goal's centre he heads at it              |
| `shotSpeed`           | 14 m/s  | how hard                                                        |
| `shotHeight`          | 0.5 m   | how high he aims                                                |
| `shotInside`          | 0.5 m   | how far inside the post                                         |
| `passRange`           | 20 m    | the farthest teammate he heads to                               |
| `knockDownRange`      | 6 m     | the farthest he heads down to                                   |
| `passSeconds`         | 0.8 s   | how long a header pass takes along the ground                   |
| `openDistance`        | 5 m     | a teammate this far from his nearest opponent is open           |
| `passWeight`          | 0.6     | what a pass to an open teammate is worth                        |
| `clearanceZone`       | 35 m    | from his own goal, how far a clearance is worth anything        |
| `clearanceDistance`   | 30 m    | how far up the pitch he clears                                  |
| `maxHeaderSpeed`      | 18 m/s  | no header leaves faster, along the ground and up together       |
| `decisionTemperature` | 0.15    | how surely he picks the best header                             |
| `directionError`      | 0.06    | how far an average header strays per meter to his aim           |
| `speedError`          | 0.1     | how much harder or softer                                       |
| `mistimedErrorFactor` | 2       | how much a fully mistimed jump widens both                      |

`validate()` rejects values outside their rules; see `aerialDuels.hpp`. The
players' side is `PlayerAttributes` ([match state](match-state.md)):
`jumping`, `heading` and `strength` in [0, 1], 0.5 by default, with
`maxSpeed` for arrival and a keeper's `keeperHandling`.

## Tests

`tests/unit/sim-match/aerialDuelsTests.cpp`:

- a jump lifts a player by his jumping, his heading times it, and off its top
  he reaches less;
- arrival, reach, strength, body position and a keeper's hands each count in
  the duel, and the stronger of two players who both reach a ball wins it
  nineteen times in twenty;
- a mistimed jump widens the header's errors, and a keeper holds a ball
  nobody challenged him for;
- a header goes at goal near it, clear near his own, down to a close teammate
  and to a farther one as a pass;
- an attacker who out-jumps the defender beside him heads a cross into the
  goal; a defender in his own box heads the same cross clear; a cross just
  above both of them is contested once and flies on as if they were not there;
  the same cross along the ground is no aerial contest, and the first to reach
  it has it;
- a keeper in his area wins a ball too high for an attacker and holds it, or
  punches it clear when he drops it.

## What this is not

Nobody but a goalkeeper lofts a pass, and nobody crosses a ball yet: a high
ball comes off a keeper's long ball
([goalkeeper distribution](goalkeeper-distribution.md)), a shot, a deflection,
a parry or the woodwork, or from a test. A keeper who holds a ball he caught in
the air distributes it like any ball in his hands. The jump is resolved at
the moment the ball arrives: a player does not leave the ground before it,
hang in the air or land somewhere else, and the movement system does not know
he jumped. There are no fouls in the air, no header on the run toward a spot
he heads for, no diving header and no chest or thigh control of a ball
between the feet and the head. A header goal earns no assist: the scorer never
received the ball, and his header shot clears a reception of his from before. Who heads how well is still the predefined attributes of
the sandbox, not a generated player.
