# Pressing

Pressing is built from individual defensive actions (PF-012) that players of a
side with a [tactic](tactics.md) choose in their
[defensive decisions](defensive-shape.md), from a challenge that can win the
ball, and from what pressure does to the carrier's pass.

## The actions

Each action's geometry is a pure function of positions (`pressingActions.hpp`),
so it can be tested on its own:

- **Pressing the carrier** (`pressCarrier`). `pressTarget()` aims `pressDistance`
  (1 m) from the carrier, on his line to the option he most likely plays -- his
  nearest teammate the presser remembers. The presser's run closes that pass
  while it closes the carrier down: an angle, not the shortest path, which would
  stop on the presser's own side of the carrier and leave the lane open. Without
  an option he aims goal-side of the carrier. Offered to a player within
  `pressRadius` (15 m) of a carrier he remembers.
- **Blocking a lane** (`blockLane`). `laneBlockTarget()` is the point of the lane
  from the carrier to the option nearest the player, at least `laneMinDistance`
  (2 m) from either end -- between them, not on top of one; the middle of a lane
  shorter than twice that. Standing there raises the carrier's own estimate of
  the interception risk of that pass: in the unit test by more than 0.3.
- **Covering the presser** (`cover`). `coverTarget()` is `coverDistance` (6 m)
  behind a teammate who presses, toward the own goal, to catch the carrier if he
  gets past. A defender reads a teammate's pressing intent from his decided
  action; without a presser, cover protects the teammate nearest the ball.

Pressing and blocking weigh the `closePressingLine` responsibility; the phase's
`pressingIntensity` and how close the player is urge them. Every candidate names
its action, target and subject in the diagnostics.

While the carrier's ball rolls ahead of him after a touch
([ball carrying](ball-carrying.md)), a player pressing him goes on at him,
not at the ball, unless he gets to it before the carrier: then the ball
pursuit system sends him after it, as after a free ball
([reception](reception.md#going-after-the-ball)) -- a touch too heavy is
there to be won.

## A carrier who stands on the ball

A side that does not press -- a deep block, a phase with a low
`pressingIntensity` -- would otherwise let a carrier stand on the ball as long
as he likes, even with a defender a step away. So the
[tactical movement](desired-region.md) sends one player at him regardless:
once the carrier has had the ball for more than `idleSeconds` (2 s) since his
last touch, the nearest outfield player of the other side within
`idlePressRadius` (3 m) of him -- the first in player order on a tie --
decides `pressCarrier`, aimed `pressDistance` goal-side of him, whatever his
phase's pressing. A role in a coordinated press comes first; a goalkeeper
holding the ball in his hands is never pressed. His teammates react through
the ordinary decisions: covering a teammate who presses, the block's shift
toward the ball. The decision trace marks the action `idlePress`. Both
values belong to `DefensiveConfig` ([defensive shape](defensive-shape.md)).

## Winning the ball

Until the duels of the P3 baseline, a placeholder challenge lets a press win the
ball at all. The "ball challenge" system (`challenge.hpp`) runs ten times a
second, after the pass decision and before movement:

1. A player whose decided action is `pressCarrier`, who stands within `radius`
   (1.2 m) of the carrier and has not challenged for `attemptSeconds` (0.5 s),
   challenges.
2. A carrier who has had the ball for less than `protectSeconds` (0.5 s) -- since
   his last touch -- is not challenged: he is still settling it, and a winner is
   not robbed straight back. Nor is a goalkeeper who holds the ball in his
   hands ([goalkeeper distribution](goalkeeper-distribution.md)), nor a carrier
   whose ball rolls ahead of him after a touch: there is nothing at his feet to
   challenge for ([ball carrying](ball-carrying.md)).
3. A challenge is one draw from the `kExecution` stream, won below `winChance`
   (0.2): the how-well half of the press, separate from the decision to press.
4. The first won challenge in player order takes the ball: the presser owns it
   and is its last touch, a pass the carrier had just decided is dropped, and the
   step records `BallWon` (winner, loser, position, cause `challenge`) and
   `PossessionChanged`.

Each player's last challenge is part of his tactical state. `ChallengeConfig` is
part of `MatchConfig` and of every replay.

## Pressure spoils passes

A pressed carrier plays worse: [pass execution](passing.md) grows both errors by
`1 + pressureErrorFactor × pressure`, where pressure is 1 − distance /
`pressureRadius` (3 m) for a lone opponent, and each further opponent within
that radius leaves the carrier less free: pressure is one minus the product of
min(1, distance / `pressureRadius`) over them. At full pressure the errors
double. So a press can also win the ball through the pass it forces -- more so
when teammates close in with the presser than when he presses alone.

## Coordinated presses

Individual actions only win the ball when teammates work together. The "team
pressing" system (`teamPressing.hpp`) runs five times a second, before the
tactical movement, for every side with a tactic (GDD section 8.7).

### Triggers

A side reacts to the triggers its tactic selects (`pressingTriggers`). A trigger
counts once an outfield player within `triggerRadius` (20 m) of the ball spots
it in what he observes: he must have seen the carrier -- or, for a slow pass,
the ball -- within the last perception interval.

| Trigger                 | Spotted when                                                    |
|-------------------------|-----------------------------------------------------------------|
| `poorFirstTouch`        | the carrier received within `recentSeconds` (1.5 s) a ball arriving at `heavyTouchSpeed` (11 m/s) or more |
| `backPass`              | the carrier just received a teammate's pass, with no shot since, that ended at least `backPassMeters` (2 m) deeper than it started |
| `receiverFacingOwnGoal` | the carrier just received and faces his own goal: the cosine to his attacking direction below `facingOwnGoal` (−0.3) |
| `isolatedReceiver`      | the carrier just received and the defender remembers no teammate of his within `isolationRadius` (10 m) |
| `slowPass`              | a recent pass to a receiver of the other side rolls slower than `slowPassSpeed` (6 m/s): the receiver is pressed before it arrives |

The facts behind them come from the match: the ball system records the last pass
(passer, origin, tick, receiver) and the last reception (player, tick, the ball's
speed) in the state.

A side in the pressing phase whose pressing intensity is at least
`phaseIntensity` (0.5) also presses whoever has the ball, without a trigger.

### Who joins, in which role

The phase's `pressingIntensity` decides how many players join: intensity times
`maxJoiners` (4), rounded. None joins at intensity below 0.125.
`assignPressRoles()` gives them roles that close the carrier's nearest options,
by where the players stand:

1. the outfield player nearest the carrier presses him;
2. from three joiners, the free player nearest the spot behind the presser
   covers him;
3. the rest block the lanes to the carrier's nearest options (his teammates but
   the goalkeeper, `keepsGoal()`), each option taken by the free player who stands nearest his
   blocking spot.

The tactical movement gives each player his role's action instead of letting him
choose -- the presser cuts off the nearest option nobody blocks -- and the
diagnostic marks it assigned. Everyone else keeps deciding for himself.

### How a press ends

| Outcome          | When                                                        |
|------------------|-------------------------------------------------------------|
| `ballRegained`   | the pressing side has the ball                               |
| `passedOut`      | another player of the carrier's side has it: played out of pressure |
| `carrierEscaped` | neither after `maxPressSeconds` (4 s)                        |

`PressingStarted` (side, carrier, trigger -- empty for a phase press -- and the
roles) and `PressingEnded` (side, outcome) are [events](match-events.md). The
press in progress is part of the state, `MatchState::press(side)`.
`PressingConfig` is part of `MatchConfig` and of every replay.

### Coordination pays

`tests/acceptance/p2AcceptanceTests.cpp` plays ten one-minute matches against the
reference tactic with home pressing high on four triggers: once at intensity
0.25, a lone presser, once at 1.0, four players. The coordinated press wins the
ball in a clearly larger share of its presses -- the lone presser leaves the
carrier's lanes open and is passed around more often, and the teammates close
to the carrier spoil the passes he does play. Over forty seeds the coordinated
press regains the ball in 0.43 of its presses, the lone presser in 0.33.
