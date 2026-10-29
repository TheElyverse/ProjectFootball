# Match clock

A match runs two halves on a clock that always shows the ninety-minute
equivalent, whatever the chosen duration (game design document §8.10). A
shorter match is shorter, not faster: running speeds, pass speeds and reaction
times stay in real seconds, and a ten-minute match ends after ten real minutes
and its added time. The clock is in `sim-match`: the configuration and the
reading in `matchClock.hpp`, the state in `MatchState::clock()`, and the system
that runs it in `matchClockSystem.hpp`, the last of the
[standard systems](match-engine.md#the-standard-systems). How players tire over
a match is part of it, see [fitness](players.md#fitness).

## Duration and the clock

`MatchClockConfig::durationMinutes` is the real minutes of play both halves last
together, without added time; 90 by default. A half lasts `halfTicks()`: half
the duration at the tick rate, to the nearest tick. The clock shows the ticks
played in the half as a share of `halfTicks()` of 45:00, in whole seconds
(`readClock()`, `formatClock()`): from 00:00 to 45:00 in the first half, from
45:00 to 90:00 in the second, and added time as `45+mm:ss` or `90+mm:ss`.
Integer arithmetic makes it exact: a ten-minute match shows at a tick what a
ninety-minute match shows at nine times that tick.

Worked examples at 30 ticks per second:

| Duration | Real time played  | Tick   | Clock      |
|----------|-------------------|--------|------------|
| 90 min   | 40 min 30 s       | 72900  | `40:30`    |
| 10 min   | 4 min 30 s        | 8100   | `40:30`    |
| 10 min   | 1 min             | 1800   | `09:00`    |
| 90 min   | 45 min, half time | 81000  | `45:00`    |
| 10 min   | 5 min, half time  | 9000   | `45:00`    |
| 10 min   | 5 min 6.7 s, first half still on | 9200 | `45+01:00` |

## Halves

Home kicks off the first half — every fixture gives home's taker the ball at
tick 0, as the [tactic match](scenarios.md#the-tactic-match) does — and away the
second. A toss that lets away open would choose both kickoffs together. The
half-time break takes no simulated time: the second half starts in the tick the
first ends, with both sides lined up for [the kickoff](match-engine.md#the-kickoff)
and started as at the beginning of a match, the ball at rest at the taker's feet
on the centre spot; when the match takes restarts it is a
[set piece](set-pieces.md), taken as away's kickoff plan says. Their fitness
carries over. A restart the [restart system](match-engine.md#restarts) took in
that tick, or a set piece still being set up, is overwritten by the kickoff;
its `RestartTaken` stays in the events.

**The teams change ends.** The engine's coordinates belong to the teams, not
to the ground: home attacks the goal at `x = length` in both halves
([team frame](match-engine.md#match-geometry)). The pitch has no end that plays
differently — no wind, slope, sun or stand — so changing ends changes no
football, only which end of the ground each side attacks. `endsChanged(period)`
says whether it has, true from the second half on; the
[debug frames](tooling.md#debug-viewer) carry it, and the viewer turns the pitch
round the centre spot for those frames. Keeping the engine's frame saves every
system and every hand-placed scenario a second set of directions.

## Added time

Stoppages cost real seconds of play, which the referee adds at the end of each
half. Every restart the [restart system](match-engine.md#restarts) takes, and
every free kick, adds its lost seconds to the half's lost time:

| Restart                     | Lost, real seconds |
|-----------------------------|--------------------|
| throw-in                    | 4                  |
| goal kick                   | 6                  |
| corner, also for a goalkeeper holding the ball too long | 6 |
| kickoff after a goal, the celebration included | 30 |
| free kick after an [offside](match-engine.md#offside) or a [foul](fouls.md) | 10 |
| penalty                     | 45                 |
| each card shown             | 15                 |

Once the half's regulation time is up, the added time is announced
(`addedMinutes()`): its lost seconds as the clock shows them — times 90 over the
duration — in whole minutes, rounded up. They are played in real time again,
`addedTicks()`: a shown minute is a 45th of `halfTicks()`. Stoppages in added
time are not added again. The half ends at the first tick after that in which
no shot is on its way, with `PeriodEnded` and the minutes that were added.

Worked examples:

- **Ninety minutes.** A half with 25 throw-ins, 6 goal kicks, 4 corners and a
  goal loses 100 + 36 + 24 + 30 = 190 s, 3.2 minutes on the clock: 4 minutes
  are added, 7200 ticks. The half ends at `45+04:00`, after 49 real minutes.
- **Ten minutes.** A half with 3 throw-ins and a goal kick loses 18 s, which the
  clock shows as nine times that, 2.7 minutes: 3 minutes are added, 20 real
  seconds, 600 ticks. Real stoppages take real time, so a short match grows
  longer by a larger share, as in the game design document.

Matches without `restarts.enabled` take no restarts and add no time.

## Full time

The second half ends like the first; then the clock stands at full time, at
`90:00` and the added minutes, and `matchResult(state)` gives the result the
application layer reads: the final score and the winner, none for a draw; it is
empty before. `MatchSimulation` runs no system after that:
steps advance the tick, and the state stays as it was at the whistle unless a
scheduled command changes it — commands still apply, nothing plays on from
them.

## Fatigue over the match

A player's fatigue over the match follows the share of the duration he has been
on the pitch, so a ten-minute match tires its players as a ninety-minute one
does: who has played nine of ten minutes is as tired as who has played 81 of
90, 90 % of the match, and has lost 0.9 of `fullMatchSpeedLoss` of his top
speed. His sprint reserve runs in real time instead, since it depends on the
single run, not on the duration. Both are in [fitness](players.md#fitness).

## Configuration and replays

`MatchClockConfig` and `FitnessConfig` are part of `MatchConfig`, as `clock` and
`fitness`, and so of every replay ([replay format](tooling.md#replay-format)).
The clock's state, `MatchClock` — period, the tick it started, its lost
seconds, its announced added minutes — is part of the state and its hash; a new
state stands at the start of the first half. Each player's `PlayerFitness` is
part of the initial snapshot.

## What this is not

No extra time or penalty shoot-out, no recovery at half time, and the referee
waits for nothing but a shot on its way. Only restarts and cards cost time
so far: injuries and substitutions will add theirs.

## From the command line

`sim-cli --duration <minutes>` sets the duration of any scenario; without
`--ticks` the run then lasts to full time. Every run stops at full time, and the
summary shows the `clock`, the `score` and the `result` — `home wins`,
`away wins`, `draw` or `not finished`:

```sh
./build/debug/apps/sim-cli/sim-cli --scenario tactic-match-eleven --seed 3 --duration 10 \
  --replay-out replay.json --stats-out stats.json
```

`--play` reports them for the replay's last tick. The
[benchmark](tooling.md#sim-benchmark) plays its `--minutes` as the duration,
every match to full time, and sums up the results.

## Tests

`tests/unit/sim-match/matchClockTests.cpp` pins the clock's arithmetic, the
ten-minute and ninety-minute equivalence, the added-time rule, fitness and the
result. `tests/acceptance/matchClockTests.cpp` plays whole ten-minute matches
through both halves, their added time and full time and replays one;
`tests/acceptance/matchResultTests.cpp` plays whole eleven-a-side matches and
checks that the result, the goals, the statistics and every kickoff after a
goal agree.
