# Duels

Two players contesting the ball: on the ground, carrier against defender, and in
the air, for a high ball and the header that follows.

- [Duels on the ball](#duels-on-the-ball)
- [Aerial duels and headers](#aerial-duels-and-headers)

## Duels on the ball

When a presser reaches a player with the ball at his feet, they fight for it.
A duel is no single roll: it is a sequence of steps, ten a second. In each, both
read position, speed, angle, balance and how well the carrier has the ball,
and both act. The carrier shields the ball, turns away with it or accelerates
past; the defender jockeys or lunges (GDD section 8.8). The result — ball kept,
won cleanly, knocked loose, or a foul — follows from the geometry of the step a
lunge lands in and the balance the earlier steps left. The model is in
`sim-match` (`duel.hpp`); pressing wins the ball through it
([pressing](team-tactics.md#winning-the-ball)).

```text
presser within engageRadius → DuelStarted, the defender sizes the carrier up
every step: ends? (kept) → balance: recovery, shoving, pushed off his feet? (won)
          → defender: jockey | commit → the lunge lands: won | loose | foul | missed
          → carrier: shield | turn away | accelerate → rolls the ball, runs
                                                       or turned off his feet (loose)
```

### Who duels

The "duel" system runs every `intervalTicks` (3 ticks, ten times a second),
after the pass decision and before movement
([match loop](match-engine.md#the-standard-systems)). At most one duel runs at
a time: the ball has one carrier.

A duel starts on a carrier with the ball at his feet — not rolling ahead after
a touch ([ball carrying](on-ball.md#ball-carrying)), not in a goalkeeper's
hands — who has had it for `protectSeconds` (0.5 s) since his last touch, so a
winner is not robbed straight back. The defender is the nearest opponent who
has decided to press the carrier (`pressCarrier`,
[defensive shape](team-tactics.md#defensive-shape)), stands within
`engageRadius` (1.5 m) and is on his feet; ties go to the first in player order.
The step records `DuelStarted`. In it the defender only jockeys, while the
carrier already decides his first move. The duel is part of the defender's
tactical state, `PlayerTacticalState::duel`: the carrier, the start, both
players' last moves and their balance.

### A step

1. **Does it go on?** It ends, **kept**, once the carrier no longer has the ball
   at his feet -- he passed, shot or touched it on, which his own
   [decisions](on-ball.md#pass-decisions) still choose --, the defender no longer
   presses him, or they stand more than `breakRadius` (3 m) apart: the carrier
   got away.
2. **Balance.** Both regain some, and bodies in contact push each other off it
   (see [balance](#balance)). A carrier pushed off his feet loses the ball:
   the defender against him has **won** it.
3. **The defender** decides, unless he is off his feet: jockey or commit. A
   lunge lands at once ([the lunge](#the-lunge)); won, loose and foul end the
   duel.
4. **The carrier** decides, unless he is about to play the ball -- a pass,
   shot or touch his decision chose this step --: shield, turn away or
   accelerate. He rolls the ball round him along the move's direction, so it
   lies `carryDistance` from him that way, and runs to the move's target --
   unless the turn took the last of his balance: then he loses the ball
   **loose**, at rest where he rolled it, in that very step. The defender
   jockeys.

Both decide from the true state, without a second look -- a duel is too quick
for perception -- by `chooseByUtility()` at `decisionTemperature` (0.2), one
draw each from the `kAi` stream. The lunge's foot draws two numbers from
`kExecution`. Nothing else is drawn, so a duel replays exactly.

The defender acts on the ball where the carrier's last move left it. So the
carrier protects the ball by reading the situation, not by reacting to a lunge:
a ball he rolled behind him is out of reach, one he dragged sideways is not.

### The geometry

Each step reads `DuelGeometry`: the carrier, the defender, the ball and the
centre of the goal the carrier attacks.

- **The way to the ball** (`wayClearance()`): how far the straight line from
  the defender to the ball passes the carrier, less `bodyRadius` (0.3 m), over
  `bodyRadius`, between 0 and 1. 0 means the way goes through him: the ball is
  shielded. In front of a carrier who faces the defender the way is clear by
  0.67, beside him by less, behind him not at all.
- **The threat** (`threat()`): how close the defender is, from 1 at
  `contactDistance` (1.2 m) to 0 at `breakRadius`, times how squarely he stands
  between the carrier and the goal -- 1 goal-side, ½ level, 0 behind him --
  times his balance. A defender who is beaten, down or far off threatens
  nothing.

### The carrier's moves

| Move         | Plays the ball                                         | Runs to                      | Utility                                  |
|--------------|--------------------------------------------------------|------------------------------|------------------------------------------|
| `shield`     | straight away from the defender, his body in between   | nowhere: he stands           | `shieldWeight` (0.8) · threat            |
| `turnAway`   | square to that, to the side of the goal                | `turnStep` (2 m) that way    | `turnWeight` (0.4) · his balance         |
| `accelerate` | toward the goal, and past the side of a defender in the way | `accelerateStep` (4 m) that way | `accelerateWeight` (0.6) · (1 − threat) |

`moveDirection()` and `moveTarget()` give the way and the target. Accelerating
leans to the side as far as the defender stands in the way to goal: straight
at the goal past a defender behind him, at 45 degrees past one squarely in
front. A pressed carrier shields the ball; one with a beaten defender goes.

Rolling the ball round him is a matter of a step, but it costs balance:
`turnCost` (0.2) for a turn right round, less for a smaller one, as
`(1 − facing · direction) / 2`. The [movement](players.md#player-movement) system then
turns him toward the ball as the duel left it, and moving faster than
`kFacingRunSpeed` he looks where he runs, so the ball goes where he goes.

### The defender's moves

| Move     | Target                                       | Utility                                         |
|----------|----------------------------------------------|-------------------------------------------------|
| `jockey` | `jockeyDistance` (1 m) goal-side of the carrier | `jockeyWeight` (0.3)                         |
| `commit` | a lunge at the ball                          | `commitWeight` (1) · way clearance · his balance |

He commits only to a ball within `tackleReach` (1.8 m) whose way passes the
carrier at all, clearance above 0: nobody lunges through a man on purpose.
Against a shielded ball he jockeys, tight enough to be in contact and push.

### The lunge

The foot aims at the ball and lands off it by a triangular error in each
direction, `footError` (0.3 m) wide at full balance, widened by `2 − balance`,
and no farther than `tackleReach` from him. The lunge costs him `commitCost`
(0.3) of balance. `resolveLunge()` then reads the geometry alone, the foot
going straight from him to where it lands:

1. **Foul**: the way passes within `bodyRadius` of the carrier before it
   meets the ball -- through the man. A foot through the ball into the man
   after it is none.
2. **Met**: it lands within `footRadius` (0.3 m) of the ball, the better the
   nearer -- 1 at the ball's centre, 0 at `footRadius`. The carrier's **hold**
   is his balance times a share from `minHold` (0.3) at a `ballControl` of 0 to
   `maxHold` (0.9) at 1.
   - better than the hold: **won** cleanly;
   - better than half the hold: knocked **loose** -- it leaves the foot at
     `looseSpeed` (12 m/s) away from where the foot met it;
   - worse: the carrier keeps it, and the lunge **missed**.
3. **Missed**: anything else.

A won ball is the defender's at his feet; a loose one is nobody's, its last
touch the defender's. A missed lunge or a foul puts the defender off his feet,
a ball knocked loose the carrier.

### Balance

Both start square on their feet, at 1. Every step

- both regain `recoveryPerSecond` (0.5 per second),
- if they stand closer than `contactDistance`, each is pushed off it by up to
  `shovePerSecond` (1 per second): half of it against an equally strong
  opponent, all of it against one stronger by half the scale or more, none
  against a weaker one by as much -- `0.5 + other's strength − own strength`,
  between 0 and 1. Between equals pushing and regaining cancel out.

and a lunge or rolling the ball round costs what is said above. Balance makes
the defender's foot less accurate, a lunge less tempting and the carrier's
hold weaker; a balanced defender threatens more. A carrier whose balance
reaches 0 is off his feet and loses the ball ([a step](#a-step)).

### Off his feet

A player who stumbles -- the defender whose lunge misses or fouls, the carrier
pushed off the ball, turned off his feet or whose ball is knocked loose, not
one tackled cleanly -- is off his feet for `stumbleSeconds` (0.5 s), in his
tactical state as `stumbledUntil` (`isStumbling()`). Until then he

- stands: the [movement](players.md#player-movement) system ignores his target,
- takes no ball: [ball movement](ball.md#ball-movement) leaves him out of who gets
  to a free or touched ball, also in the step he stumbled in, and a keeper
  neither dives for a shot nor faces one,
- chases none: [pursuit](ball.md#going-after-the-ball) sends a teammate,
- starts no duel, and in one he is in he only jockeys, pushes nobody and wins
  no ball by pushing the carrier off his feet.

A restart gets its taker up again ([restarts](match-engine.md#restarts)).

So a loose ball is scrappy: the carrier is down, the defender cannot retake
his own touch during the reclaim delay, and the quickest player gets it. A
beaten defender stands while the carrier accelerates away.

### How a duel ends

| Outcome | When                                                       | Events                                                       |
|---------|------------------------------------------------------------|--------------------------------------------------------------|
| `kept`  | the carrier played the ball on, got more than `breakRadius` away, or the defender stopped pressing | `DuelEnded`                       |
| `won`   | a lunge met the ball better than the carrier's hold, or the defender pushed him off his feet | `TackleAttempted` for a lunge, `DuelEnded`, `BallWon` (cause `duel`), `PossessionChanged` |
| `loose` | a lunge met it, but not that well, or the carrier turned himself off his feet | `TackleAttempted` for a lunge, `DuelEnded`, `PossessionChanged` to nobody |
| `foul`  | a lunge went through the man                                 | `TackleAttempted`, `DuelEnded`                               |

A foul leaves the carrier on the ball and the defender off his feet; with the
[foul rules](fouls.md) on, the referee then gives the free kick, the penalty or
advantage, and the card. A won or loose ball drops the carrier's pending pass, shot or touch,
which the ball system would discard anyway
([ball movement](ball.md#ball-movement)). `DuelStarted` and `DuelEnded`
name the carrier and the defender and where the ball was -- for a carrier who
turned himself off his feet, where he rolled it ([match events](match-engine.md#match-events-and-decision-diagnostics)).

### Tackles and blocks

A **tackle** is the defender's lunge in a duel: an action with a commitment
(GDD §8.6). It wins the ball, knocks it loose, misses and leaves him off his
feet, or goes through the man -- all from where his foot lands against the ball
and the carrier's body ([the lunge](#the-lunge)), never from a roll of its own.
Its timing is the duel's: he lunges at the ball where the carrier's last move
left it, so a carrier who rolled it behind him is out of reach, and one whose
way to the ball passes through the carrier is not lunged at at all. That is
**shielding**: the carrier keeps his body between ball and defender
([the carrier's moves](#the-carriers-moves)), the way to the ball has no
clearance, and a clean tackle is impossible -- only a foul or jockeying is left.

A **block** is the same lunge at a shot. A shot fast enough to come off a body
([shooting](on-ball.md#on-the-way)) meets an outfield player whose body is in
its way -- within the shooting's `blockRadius` (0.5 m), no higher than
`blockReach` (1.8 m) -- as before. An opponent of the shooter on his feet
lunges at it, first come first served along its way, when

- it comes at him: he stands ahead of the ball along its travel,
- it would not hit his body anyway: its line passes him farther than
  `blockRadius`,
- it passes him within `lungeRadius` (1 m), no higher than `lungeHeight`
  (0.9 m),
- and before it gets to the keeper facing it.

His foot lands as a duel's lunge does at full balance (`landLunge()`): off the
ball by a triangular draw in each direction of half-width `footError` (0.3 m),
two draws from `kExecution`. Within `footRadius` (0.3 m) of the ball it
**blocks** the shot: it comes off him like off a body. Missing, he is off his
feet for `stumbleSeconds` (0.5 s), and the shot flies on to whoever is next in
its way. A block is a reflex, not a choice: in the time a shot takes nobody
weighs jockeying against it, so every opponent the shot comes at in reach
lunges.

The ball's height decides which of the two he can do: a low shot he can block
from a meter away, a shot at chest height only with his body, one over his head
not at all. The keeper does neither; he [meets](goalkeeper.md#shot-stopping) a
shot his own way.

Every lunge records **`TackleAttempted`**: the tackler, the carrier -- the
shooter for a block --, whether it was a `block`, what it did (`won`, `loose`,
`missed`, `foul` or `blocked`) and where the ball was. A tackle's comes before
the `DuelEnded` it ends the duel with, a block's before its `ShotDeflected`.
The [match events](match-engine.md#match-events-and-decision-diagnostics) carry a
foul to the rules: the lunge that went through the man is where the
[foul system](fouls.md) gives the free kick. The tackle tests are in `duelTests.cpp` -- where
the foot lands decides a clean tackle, one through the player and a miss, and
nobody commits to a shielded ball -- and every lunge of the duel scenarios is
checked in `duelScenarioTests.cpp`, fouls included.

### Configuration

`DuelConfig` is part of `MatchConfig` (`duels`) and of every replay;
`validate()` rejects values outside their ranges.

| Field                | Default | Meaning                                                      |
|----------------------|---------|--------------------------------------------------------------|
| `intervalTicks`      | 3       | ticks between the steps of a duel                            |
| `protectSeconds`     | 0.5 s   | a carrier is not taken on before he has had the ball this long |
| `engageRadius`       | 1.5 m   | a presser this close takes him on                            |
| `breakRadius`        | 3 m     | they part this far apart                                     |
| `jockeyDistance`     | 1 m     | a jockeying defender stays this far goal-side                |
| `turnStep`, `accelerateStep` | 2 m, 4 m | how far ahead a carrier runs turning away and accelerating |
| `tackleReach`        | 1.8 m   | how far a lunge reaches                                      |
| `footRadius`         | 0.3 m   | a foot this close to the ball meets it                       |
| `bodyRadius`         | 0.3 m   | how wide the carrier's body is                               |
| `footError`          | 0.3 m   | how far off the ball a balanced foot lands at most           |
| `minHold`, `maxHold` | 0.3, 0.9 | the carrier's hold at a `ballControl` of 0 and of 1         |
| `looseSpeed`         | 12 m/s  | how fast a ball knocked loose leaves                         |
| `contactDistance`    | 1.2 m   | bodies this close push each other                            |
| `shovePerSecond`     | 1       | the most balance pushing takes a second                      |
| `commitCost`, `turnCost` | 0.3, 0.2 | the balance a lunge and a turn right round cost        |
| `recoveryPerSecond`  | 0.5     | the balance everyone regains a second                        |
| `stumbleSeconds`     | 0.5 s   | how long a player who stumbles is off his feet               |
| `shieldWeight` ... `commitWeight` | 0.8, 0.4, 0.6, 0.3, 1 | the moves' weights               |
| `decisionTemperature` | 0.2    | the temperature they are chosen at                           |

With them, the players' `strength` and `ballControl` in `PlayerAttributes`, and
the ball physics' `carryDistance`. A block's reach is the shooting's
`lungeRadius` and `lungeHeight` ([tackles and blocks](#tackles-and-blocks)); its
foot and stumble are the duel's.

### Scenarios

The [duel scenarios](scenarios.md#the-duel-scenarios) give the idle carrier's
centre back opponents of different strength: in `duel-ball-won` a strong centre
back wins the ball from a weak carrier, in `duel-ball-kept` a strong carrier
keeps it from a weak one, and in `duel-loose-ball` an even duel knocks it loose
for others to chase.

### What this is not

- No tackle but the lunge: no slide, no shoulder-to-shoulder duel on the run.
  A block is the same lunge at a shot ([tackles and blocks](#tackles-and-blocks));
  a [cross](on-ball.md#crossing) is not blocked.
- No feint, and no dribble inside a duel: a carrier who touches the ball on
  leaves the duel and [dribbles](on-ball.md#dribble).
- Without the [foul rules](fouls.md) a foul has no consequence but the defender's stumble: no free kick, card or
  advantage.
- Bodies block only the way of a foot, not runs; players turn at once.
- Duels for a ball in the air are [aerial duels](#aerial-duels-and-headers).

## Aerial duels and headers

When a high ball comes to a player, everyone near enough goes up for it. Each
jump has a timing and a reach; among those who reach the ball, a duel decides
the winner. He heads it — at goal, to a teammate, down to one close by, or
clear — and a goalkeeper in his area catches it or punches it clear
(implementation plan §6.1, stage M6; GDD §8.8). The model is in `sim-match`
(`aerialDuels.hpp`); the [ball system](ball.md#ball-movement) plays it out
where the ball meets the players.

```text
high ball → first contact: findAerialContact()
          → who goes up: findChallengers()
          → jumps: timing, reach (kExecution)
          → duel: duelUtility(), chooseByUtility() (kExecution)
          → the winner's touch: decideHeader() (kAi), executeHeader() (kExecution), or hold / punch
```

### In the air or on the ground

The ball's height decides. A ball no higher than `controlHeight` (1 m) where a
player reaches it is taken at his feet
([reception](ball.md#reception-interceptions-and-loose-balls)): the first to
reach it has it. Above that it is contested in the air.

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

A ball reached in the air no later than anyone takes it at his feet is
contested. A ball that never rises above `controlHeight` in the tick is not
searched (`mayRiseAbove()`). In his area a keeper's hands contest a ball above
`controlHeight` like anyone; uncontested he holds it.

A player who went up is **in the air** for `landingSeconds` (0.6 s)
(`isInTheAir()`, the `lastJump` of his tactical state). Until he lands he goes
up for no other ball, so the same players do not contest a ball nobody reached
again, nor take it at their feet in the same tick. It flies on to the keeper
who faces it and to whoever reaches it next. Like reception, the contest leaves
out a keeper busy with a dive or facing a shot
([shot stopping](goalkeeper.md#shot-stopping)) and the ball's last touch within
`reclaimDelaySeconds` (`mayCompete()`). A shot fast enough to come off a body is
no aerial ball: it is [deflected](on-ball.md#on-the-way).

### Who goes up

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

### The jump

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

### The duel

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
second, and `strengthWeight`, `bodyWeight` and `keeperAdvantage` 1. Arriving
0.1 s late costs half a point — half the gap between the weakest and the
strongest player.

If nobody reaches the ball, it flies on as if they were not there.

### The keeper's hands

A keeper who wins a ball with his hands holds it with `holdChance()`:
certainly if no opponent went up with him; otherwise all but `contestedDrop`
(0.3) of the time for a keeper of average handling, the share scaled by
`skillErrorFactor()` of his `keeperHandling`. One draw from `kExecution`,
always. Held, he has the ball like any player who takes a free ball. Dropped,
he punches it: it leaves his fists as a clearance would leave a head, his
errors scaled by his handling rather than heading.

So handling helps the keeper three times: his hands reach higher and farther
than a head, it counts in the duel, and it decides whether he holds the ball.

### The header

The winner decides his header like every decision, from options with
utilities chosen by `chooseByUtility()` — at `decisionTemperature` (0.15), one
draw from the `kAi` stream (`decideHeader()`). A header is played without a
second look, so `headerOptions()` reads the true state rather than his
[perception](players.md#perception):

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
`maxHeaderSpeed` in all; an aim on the ball itself is headed along his
facing. Both errors are scaled by `headerErrorFactor()`: `skillErrorFactor()` of his
heading, times `1 + mistimedErrorFactor · mistiming` — a fully mistimed header
is three times as wide. The ball leaves without spin.

What the ball is afterwards follows the play:

- a **shot** is a new [shot](on-ball.md#shooting): `ShotAttempted`, the keeper faces
  it, and it ends in its `ShotResolved`;
- a **pass** or **knock-down** is a pass: `PassAttempted`, and a teammate who
  takes it received it, an opponent intercepted it;
- a **clearance** or a **punch** is no pass: whoever takes it recovers a loose
  ball.

A shot still open when the ball was headed or punched has become what it will
be, as if the player had taken it ([shooting](on-ball.md#outcomes)). The ball
flies on from the head for the rest of the tick: the keeper who faces a
header shot meets it where it passes him, and it comes off the goal frame,
but nobody else gets to it before the next tick.

### Events

`AerialContest` records every contest, with where the ball was and how high
when the first player got to it, every `contestants` — the player, the
`reach` his jump met it with and whether he `reached` it —, the `winner`, empty
if nobody reached it, and his `play`: `shot`, `pass`, `knockDown`,
`clearance`, `caught` or `punched` ([match events](match-engine.md#match-events-and-decision-diagnostics)). The
events of the play follow it.

### Configuration

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
players' side is `PlayerAttributes` ([match state](match-engine.md#match-state)):
`jumping`, `heading` and `strength` in [0, 1], 0.5 by default, with
`maxSpeed` for arrival and a keeper's `keeperHandling`.

### Tests

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

### What this is not

- No crosses, and only goalkeepers loft passes: a high ball comes from a
  keeper's long ball ([goalkeeper distribution](goalkeeper.md#distribution)), a
  shot, a deflection, a parry, the woodwork or a test. A keeper who catches a
  ball distributes it like any ball in his hands.
- The jump happens at the ball's arrival: no take-off before it, no hang time,
  no landing elsewhere; the movement system does not know he jumped.
- No fouls in the air, no header on the run, no diving header, no chest or
  thigh control.
- A header goal earns no assist: the scorer never received the ball, and his
  header shot clears an earlier reception of his.
- Heading skill comes from the sandbox's predefined attributes, not generated
  players.
