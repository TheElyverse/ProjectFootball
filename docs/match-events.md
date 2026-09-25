# Match events and decision diagnostics

A match produces two kinds of output besides its state: **events**, immutable
facts about what happened, and **decision diagnostics**, the reasoning behind
what players decided. Both live in `sim-match` (`matchEvents.hpp`).

## Events

Systems record events through `MatchStepContext::record()` while they change the
state; commands that change possession record theirs before the systems run.
After a successful step, `MatchSimulation::events()` holds the events of that
step in the order they were recorded — the "publish" slot of the
[match loop](match-loop.md). A failed step publishes nothing: `events()` keeps
the last good step's events.

Every event carries the `tick` of its step and the players involved:

| Event                | Fields                                                       | Recorded when                               |
|----------------------|--------------------------------------------------------------|---------------------------------------------|
| `PassAttempted`      | `passer`, `intendedReceiver`, `from`, `target`, `speed`      | a pass leaves the passer's foot             |
| `PassReceived`       | `receiver`, `passer`                                         | a teammate of the passer takes the pass     |
| `PassIntercepted`    | `interceptor`, `passer`                                      | an opponent of the passer takes the pass    |
| `LooseBallRecovered` | `player`                                                     | someone takes a ball nobody played, or the passer takes his own pass back |
| `PossessionChanged`  | `previousOwner`, `newOwner`                                  | the ball's owner changes; either may be empty |
| `PhaseChanged`       | `side`, `previous`, `phase`                                  | a side with a tactic enters another [tactical phase](match-phases.md); `previous` is empty for its first |

`speed` is the speed the ball left the foot with, execution error included. A
pass shows up as

```text
PassAttempted(t, passer)  PossessionChanged(t, passer → none)
… PassReceived(t', receiver, passer)  PossessionChanged(t', none → receiver)
```

**Classification.** When a player takes a free ball, the ball's last touch
decides what it was. In P1 a ball becomes free only by a pass, so a last touch
means the ball was passed: a teammate of the passer received it, an opponent
intercepted it. Without a last touch, or when the passer takes his own ball
back, the ball was loose.

Events are part of the match: the same setup produces the same events in the
same order. `addEvent()` feeds an event into a `StableHasher` — its type, tick
and every field — so replays can compare event sequences as well as states.
`eventName()` names an event for logs.

## Decision diagnostics

A `DecisionDiagnostic` explains one decision of a player on the ball (see
[pass decisions](pass-decisions.md)):

| Field          | Meaning                                                     |
|----------------|-------------------------------------------------------------|
| `tick`         | the tick of the decision                                    |
| `player`       | the player on the ball                                      |
| `observations` | his perception memory at that moment                        |
| `candidates`   | every option with all score components and rejection reason |
| `outcome`      | `kPassed` or `kNoValidOption`                               |
| `chosen`       | index of the chosen candidate, if he passed                 |

An `ActionDiagnostic` explains one decision of a player without the ball (see
[off-ball movement](off-ball-movement.md)): the `tick`, the `player`, every
`candidates` action with its target, subject, weighted `scores` and utility, and
the index of the `chosen` one. `MatchSimulation::actionDiagnostics()` holds those
of the last step.

Diagnostics are off by default. `MatchSimulation::setCollectDiagnostics(true)`
turns them on; `diagnostics()` then holds the diagnostics of the last step.

Diagnostics are read-only output: the decision system builds them only on
request and only after it has chosen, so they cannot change a decision, and
building them draws no random numbers. They are never hashed and no system reads
them. A test runs the same match with and without diagnostics and requires
identical states and events at every tick.

## What this is not

There is no event history inside the simulation — each step publishes its own
events, and whoever needs a log keeps one — and no match statistics yet:
possession share, pass completion and similar metrics belong to analytics built
on these events.
