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

## Winning the ball

Until the duels of the P3 baseline, a placeholder challenge lets a press win the
ball at all. The "ball challenge" system (`challenge.hpp`) runs ten times a
second, after the pass decision and before movement:

1. A player whose decided action is `pressCarrier`, who stands within `radius`
   (1.2 m) of the carrier and has not challenged for `attemptSeconds` (0.5 s),
   challenges.
2. A carrier who has had the ball for less than `protectSeconds` (0.5 s) -- since
   his last touch -- is not challenged: he is still settling it, and a winner is
   not robbed straight back.
3. A challenge is one draw from the `kExecution` stream, won below `winChance`
   (0.2): the how-well half of the press, separate from the decision to press.
4. The first won challenge in player order takes the ball: the presser owns it
   and is its last touch, a pass the carrier had just decided is dropped, and the
   step records `BallWon` (winner, loser, position) and `PossessionChanged`.

Each player's last challenge is part of his tactical state. `ChallengeConfig` is
part of `MatchConfig` and of every replay.

## Pressure spoils passes

A pressed carrier plays worse: [pass execution](passing.md) grows both errors by
`1 + pressureErrorFactor × pressure`, where pressure is 1 − distance /
`pressureRadius` (3 m) for the nearest opponent. At full pressure the errors
double. So a press can also win the ball through the pass it forces.
