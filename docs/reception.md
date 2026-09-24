# Reception, interceptions and loose balls

A free ball belongs to whoever reaches it first. **Reception** (`reception.hpp`)
in `sim-match` resolves, every tick, which player gains control of a free ball on
its way.

## Gaining control

The [ball movement](ball-movement.md) system rolls a free ball from where it is
at the start of a tick to where it is at the end. During the same tick every
player moves as the movement system moves him. `findBallClaim()` checks every
player against the ball:

- **Reach.** Player and ball both move in a straight line over the tick. A
  player gains the ball if they come within `controlRadius` (1 m) of each other
  at any moment of the tick — so a fast pass cannot slip through a player
  between two ticks. `findContact()` computes the first such moment.
- **No instant reclaim.** The ball's last touch cannot take it back for
  `reclaimDelaySeconds` (0.3 s) after touching it, so a pass does not stick to
  the passer's foot.
- **Competing claims.** The earliest contact in the tick wins. At the same
  moment the player who comes closer wins, and at the same distance the lower
  player id: deterministic whatever the order of players in the state.

The winner owns the ball from the end of the tick, with the ball at his feet
(see [possession](possession.md)), and becomes its last touch.

The same rule covers every way of getting the ball:

| Situation     | What it looks like                                                     |
|---------------|------------------------------------------------------------------------|
| reception     | a teammate of the passer reaches the pass                              |
| interception  | an opponent of the passer reaches the pass on its way                  |
| missed pass   | nobody reaches it; it rolls on until it stops or leaves the pitch      |
| loose ball    | a ball at rest, or rolling slowly, is recovered by the first to reach it |

Telling these apart is for match events; control itself does not care who
kicked the ball.

## Configuration

`ReceptionConfig` is part of `MatchConfig` and of every replay:

| Field                           | Default | Meaning                                         |
|---------------------------------|---------|-------------------------------------------------|
| `reception.controlRadius`       | 1 m     | how close a free ball must come to be controlled |
| `reception.reclaimDelaySeconds` | 0.3 s   | how long the last touch cannot take the ball back |

## What this is not

There is no first-touch quality, no deflection and no failed control: a player
who reaches the ball has it. Pressing a player on the ball, tackles and
duels are out of scope.
