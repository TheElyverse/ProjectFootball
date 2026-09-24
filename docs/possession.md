# Possession

The ball is either free or controlled by exactly one player. Possession lives in
the ball state of `sim-match` (`matchState.hpp`); the
[ball movement](ball-movement.md) system keeps a controlled ball with its carrier.

## State

`BallState::owner` is the id of the player in control of the ball, empty while
the ball is free; `isControlled()` tells the two apart.

- **At most one owner.** Possession is one optional id on the ball, not a flag
  per player, so two players can never own the ball at the same time and a
  player and the ball can never disagree about who has it.
- **Only players in the state.** `MatchState::create()` rejects an owner no player
  in the state has (`kUnknownBallOwner`), and `MatchStateWriter::setBallOwner()`
  throws `std::invalid_argument` for one.

## A controlled ball follows its carrier

A controlled ball sits `carryDistance` (0.5 m) ahead of its carrier along his
facing and moves with his velocity:

```text
ball.position = carrier.position + carrier.facing · carryDistance
ball.velocity = carrier.velocity
```

The ball movement system applies this rule every tick, to the carrier as the
movement system leaves him after the same tick: it moves and turns him with the
same pure functions, `stepPlayerMovement()` and `facingAfterMove()`, so ball and
carrier end every step together. `carriedBallPosition(carrier, physics)` computes
the position.

This makes the two systems a pair: the ball system predicts the carrier's move,
so the prediction only comes true if the player movement system runs in the same
tick. `makeMatchSystems()` always installs both, every tick. A custom system
list that runs the ball system without the movement system, or at another
interval, may only use it for free balls; with a controlled ball it would leave
the ball where the carrier would have gone. Systems read the state from before
the step and cannot see what another system writes, so the ball cannot simply
follow the carrier's committed position instead.

Because the ball's velocity is the carrier's in every tick it is controlled, no
velocity from before survives a change of possession: a player who takes a rolling
ball stands with it at his feet, at his own speed. When a ball is released — by a
pass, for example — whoever releases it sets its new velocity.

## Changing possession

| Change                  | How                                                          |
|-------------------------|--------------------------------------------------------------|
| a scenario or test gives the ball to a player | `GiveBallCommand{playerId}`                    |
| a player receives, intercepts or recovers it  | the reception system (coming with passing)     |
| a player passes         | pass execution releases it with the pass velocity (coming)   |

A `GiveBallCommand` applies at the start of its tick; the ball is at the new
owner's feet at the end of that tick. Possession changes are state changes like
any other, so replays reproduce them and the state hash covers the owner.

## Configuration

`BallPhysics::carryDistance` (0.5 m by default) is part of `MatchConfig` and of
every replay. It must be finite and not negative.

## What this is not

There is no dribbling skill, no loss of control and no tackling: a carrier keeps
the ball until something takes it from him deliberately.
