# Match phases

A [tactic](tactics.md) gives instructions per tactical phase, so the match has to
know which phase each team is in. The tactical phase system (`tacticalPhases.hpp`)
decides that from possession, the ball's position and recent possession changes.

## Team possession

`MatchState::possession()` records which team has the ball:

- the team of the ball's owner, or, while the ball is free, the team of its last
  touch -- a pass in flight still belongs to the passer's team, and a loose ball
  to whoever played it last;
- `since`, the tick the team won the ball: the tick of the ball's last touch when
  the change is noticed;
- `fromOpponent`, whether it took the ball from the other team rather than from
  nobody, as at kickoff.

Possession is tracked for every match, with or without tactics.

## Phases

A side with a tactic is in one of the seven phases of [tactics](tactics.md):

| Situation                         | Phase                                                          |
|-----------------------------------|----------------------------------------------------------------|
| won the ball from the opponent less than `transitionSeconds` ago | `attackingTransition`           |
| lost the ball to the opponent less than `transitionSeconds` ago  | `defensiveTransition`           |
| has the ball, ball in the own third      | `buildUp`                                               |
| has the ball, ball in the middle third   | `progression`                                           |
| has the ball, ball in the opponent's third | `finalThird`                                          |
| without the ball, ball at least `pressingLine` up the pitch | `pressing`                           |
| without the ball, ball below the pressing line | `defensiveBlock`                                  |

Distances up the pitch are measured from the team's own goal line
(`depthOf()`, `teamFrame.hpp`); `pressingLine` is the tactic's principle, a
fraction of the pitch length. While nobody has had the ball the rules without
the ball apply, with no transition. A scripted side, one without a tactic, has
no phase.

**Hysteresis.** A team leaves a third only once the ball is `hysteresisMeters`
past the boundary into the neighbouring third, and switches between pressing and
block only once the ball is that far past the pressing line. A ball rolling
along a boundary keeps the phase it had instead of flickering between two. A
ball that jumps two thirds at once, and the end of a transition, switch at once.

## The system

| Parameter           | Default | Meaning                                              |
|---------------------|---------|------------------------------------------------------|
| `intervalTicks`     | 10      | runs every 10 ticks, 3 Hz at 30 Hz: the tactical evaluation rate of implementation plan section 6.3 |
| `transitionSeconds` | 4.0     | how long a transition lasts after a possession change |
| `hysteresisMeters`  | 3.0     | margin past a boundary before the phase switches      |

`PhaseConfig` is part of `MatchConfig` and of every replay. The system runs
second in the [standard order](match-loop.md), after perception; it writes team
possession and the phases, nothing else. When a side's phase changes it records
a `PhaseChanged` [event](match-events.md) with the side, the previous phase
(empty for the first) and the new one.

`classifyPhase()` is the rule above as a pure function of a `PhaseSituation`, and
`updatePossession()` the possession update, so both can be tested and reused
without running a match.

## Example

`tests/unit/sim-match/tacticalPhasesTests.cpp` plays the kickoff fixture with the
reference tactic for both sides. Home's forward gets the ball on the centre spot
at tick 0: home is in `progression`, away in `defensiveBlock`. At tick 60 away's
goalkeeper gets the ball: home enters `defensiveTransition` and away
`attackingTransition`. Four seconds later, at tick 180, away is in `buildUp` and
home, with the ball deep in away's half beyond its pressing line, in `pressing`.
