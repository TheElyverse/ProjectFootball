# Duels

When a presser gets to a player with the ball at his feet, they fight for it.
A duel is not one roll for a winner: it is a short sequence, a step ten times a
second, in which both players read where they stand, how fast, at what angle,
how well balanced and how well the carrier has the ball, and both act -- the
carrier shields the ball, turns away with it or accelerates past, the defender
jockeys or commits to a lunge (GDD section 8.8). What comes of it -- the ball
kept, won cleanly, knocked loose, or a foul -- follows from the geometry of the
step a lunge lands in and from the balance the steps before left both players
with. The model lives in `sim-match` (`duel.hpp`) and replaces the placeholder
challenge pressing used to win the ball with ([pressing](pressing.md#winning-the-ball)).

```text
presser within engageRadius → DuelStarted, the defender sizes the carrier up
every step: ends? (kept) → balance: recovery, shoving, off his feet? (won / loose)
          → defender: jockey | commit → the lunge lands: won | loose | foul | missed
          → carrier: shield | turn away | accelerate → rolls the ball, runs
```

## Who duels

The "duel" system runs every `intervalTicks` (3 ticks, ten times a second),
after the pass decision and before movement, in the slot the challenge had
([match loop](match-loop.md#the-standard-systems)). At most one duel is fought
at a time: the ball has one carrier.

A duel starts on a carrier who has the ball at his feet -- not rolling ahead of
him after a touch ([ball carrying](ball-carrying.md)), not in a goalkeeper's
hands -- and has had it for `protectSeconds` (0.5 s) since his last touch: he is
still settling it, and a winner is not robbed straight back. The defender is
the nearest player of the other side whose decided action is to press the
carrier (`pressCarrier`, [defensive shape](defensive-shape.md)), who stands
within `engageRadius` (1.5 m) of him and is on his feet; ties to the first in
player order. The step records `DuelStarted`. In it the defender only sizes the
carrier up -- he jockeys -- while the carrier already decides his first move.
The duel is part of the defender's tactical state, `PlayerTacticalState::duel`:
the carrier, when it began, both players' last moves and their balance.

## A step

1. **Does it go on?** It ends, **kept**, once the carrier no longer has the ball
   at his feet -- he passed, shot or touched it on, which his own
   [decisions](pass-decisions.md) still choose --, the defender no longer
   presses him, or they stand more than `breakRadius` (3 m) apart: the carrier
   got away.
2. **Balance.** Both regain some, and bodies in contact push each other off it
   (see [balance](#balance)). A carrier off his feet loses the ball.
3. **The defender** decides, unless he is off his feet: jockey or commit. A
   lunge lands at once ([the lunge](#the-lunge)); won, loose and foul end the
   duel.
4. **The carrier** decides, unless he is about to play the ball -- a pass,
   shot or touch his decision chose this step --: shield, turn away or
   accelerate. He rolls the ball round him along the move's direction, so it
   lies `carryDistance` from him that way, and runs to the move's target. The
   defender jockeys.

Both decide from the true state, without a second look -- a duel is too quick
for perception -- by `chooseByUtility()` at `decisionTemperature` (0.2), one
draw each from the `kAi` stream. The lunge's foot draws two numbers from
`kExecution`. Nothing else is drawn, so a duel replays exactly.

Because the defender acts on the ball where the carrier's last move left it,
the carrier protects the ball by reading the situation, not by reacting to a
lunge already on its way: a ball he rolled behind him a step ago is out of
reach, one he dragged to the side to get away is not.

## The geometry

Each step reads `DuelGeometry`: the carrier, the defender, the ball and the
centre of the goal the carrier attacks.

- **The way to the ball** (`wayClearance()`): how far the straight line from
  the defender to the ball passes the carrier, less `bodyRadius` (0.35 m), over
  `bodyRadius`, between 0 and 1. 0 means the way goes through him: the ball is
  shielded. In front of a carrier who faces the defender the way is clear by
  0.43, beside him by less, behind him not at all.
- **The threat** (`threat()`): how close the defender is, from 1 at
  `contactDistance` (1.2 m) to 0 at `breakRadius`, times how squarely he stands
  between the carrier and the goal -- 1 goal-side, ½ level, 0 behind him --
  times his balance. A defender who is beaten, down or far off threatens
  nothing.

## The carrier's moves

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
`(1 − facing · direction) / 2`. The [movement](player-movement.md) system then
turns him toward the ball as the duel left it, and moving faster than
`kFacingRunSpeed` he looks where he runs, so the ball goes where he goes.

## The defender's moves

| Move     | Target                                       | Utility                                         |
|----------|----------------------------------------------|-------------------------------------------------|
| `jockey` | `jockeyDistance` (1 m) goal-side of the carrier | `jockeyWeight` (0.3)                         |
| `commit` | a lunge at the ball                          | `commitWeight` (1) · way clearance · his balance |

He commits only to a ball within `tackleReach` (1.8 m) whose way passes the
carrier at all, clearance above 0: nobody lunges through a man on purpose.
Against a shielded ball he jockeys, tight enough to be in contact and push.

## The lunge

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
     `looseSpeed` (5 m/s) away from where the foot met it;
   - worse: the carrier keeps it, and the lunge **missed**.
3. **Missed**: anything else.

A won ball is the defender's at his feet; a loose one is nobody's, its last
touch the defender's. A missed lunge or a foul puts the defender off his feet,
a ball knocked loose the carrier.

## Balance

Both start square on their feet, at 1. Every step

- both regain `recoveryPerSecond` (0.5 per second),
- if they stand closer than `contactDistance`, each is pushed off it by up to
  `shovePerSecond` (1 per second): half of it against an equally strong
  opponent, all of it against one stronger by half the scale or more, none
  against a weaker one by as much -- `0.5 + other's strength − own strength`,
  between 0 and 1. Between equals pushing and regaining cancel out.

and a lunge or rolling the ball round costs what is said above. Balance makes
the defender's foot less accurate, a lunge less tempting and the carrier's
hold weaker; a balanced defender threatens more.

A carrier whose balance reaches 0 is **off his feet** and loses the ball: to a
defender in contact with him, who pushed him off it -- **won** --, or, turned
off his feet by his own turns, where it lies -- **loose**, at rest.

## Off his feet

A player who stumbles -- the defender whose lunge misses or fouls, the carrier
who loses the ball -- is off his feet for `stumbleSeconds` (0.5 s), in his
tactical state as `stumbledUntil` (`isStumbling()`). Until then he

- stands: the [movement](player-movement.md) system ignores his target,
- takes no ball: [ball movement](ball-movement.md) leaves him out of who gets
  to a free or touched ball, also in the step he stumbled in, and a keeper
  faces no shot,
- chases none: [pursuit](reception.md#going-after-the-ball) sends a teammate,
- starts no duel, and in one he is in he only jockeys, pushes nobody and wins
  no ball by pushing the carrier off his feet.

So a loose ball is scrappy: the carrier is down, the defender cannot take back
his own touch for the reclaim delay, and whoever is quickest to it gets it. A
beaten defender stands while the carrier accelerates away from him.

## How a duel ends

| Outcome | When                                                       | Events                                                       |
|---------|------------------------------------------------------------|--------------------------------------------------------------|
| `kept`  | the carrier played the ball on, got more than `breakRadius` away, or the defender stopped pressing | `DuelEnded`                       |
| `won`   | a lunge met the ball better than the carrier's hold, or the defender pushed him off his feet | `DuelEnded`, `BallWon` (cause `duel`), `PossessionChanged` |
| `loose` | a lunge met it, but not that well, or the carrier turned himself off his feet | `DuelEnded`, `PossessionChanged` to nobody |
| `foul`  | a lunge went through the man                                 | `DuelEnded`                                                  |

A foul is only recorded for now: play goes on with the carrier on the ball, and
the defender off his feet. Whistling it, the free kick and cards come with the
fouls of the P3 rules baseline. A won or loose ball drops
the carrier's pending pass, shot or touch, as the ball system would discard
it anyway ([ball movement](ball-movement.md)). `DuelStarted` and `DuelEnded`
name the carrier and the defender and where the ball was ([match events](match-events.md)).

## Configuration

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
| `bodyRadius`         | 0.35 m  | how wide the carrier's body is                               |
| `footError`          | 0.3 m   | how far off the ball a balanced foot lands at most           |
| `minHold`, `maxHold` | 0.3, 0.9 | the carrier's hold at a `ballControl` of 0 and of 1         |
| `looseSpeed`         | 5 m/s   | how fast a ball knocked loose leaves                         |
| `contactDistance`    | 1.2 m   | bodies this close push each other                            |
| `shovePerSecond`     | 1       | the most balance pushing takes a second                      |
| `commitCost`, `turnCost` | 0.3, 0.2 | the balance a lunge and a turn right round cost        |
| `recoveryPerSecond`  | 0.5     | the balance everyone regains a second                        |
| `stumbleSeconds`     | 0.5 s   | how long a player who stumbles is off his feet               |
| `shieldWeight` ... `commitWeight` | 0.8, 0.4, 0.6, 0.3, 1 | the moves' weights               |
| `decisionTemperature` | 0.2    | the temperature they are chosen at                           |

With them, the players' `strength` and `ballControl` in `PlayerAttributes`, and
the ball physics' `carryDistance`.

## Scenarios

The [duel scenarios](scenarios.md#the-duel-scenarios) give the idle carrier's
centre back opponents of different strength: in `duel-ball-won` a strong centre
back wins the ball from a weak carrier, in `duel-ball-kept` a strong carrier
keeps it from a weak one, and in `duel-loose-ball` an even duel knocks it loose
for others to chase.

## What this is not

There is no tackle but the lunge: no slide, no block of a shot or a cross, and
no shoulder-to-shoulder duel while both run. There is no feint, and no
dribble past a defender inside a duel -- a carrier who touches the ball on
leaves the duel and [dribbles](ball-carrying.md#dribble). A foul has no
consequence but the defender's stumble; no free kick, no card, no advantage.
Players' bodies do not block each other's runs, only the way of a foot, and a
player turns at once rather than at a rate. Duels for a ball in the air are
[aerial duels](aerial-duels.md).
