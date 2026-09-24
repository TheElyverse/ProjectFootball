# Scenarios

A scenario is a named, reproducible match setup: a fixture, the configuration of
the standard systems, and scripted commands. The seed is the caller's; everything
else is fixed, so a scenario name and a seed identify a match. The catalog lives
in `sim-match` (`scenarios.hpp`), and `sim-cli --scenario <name>` runs one.

| Name           | Setup                                                             |
|----------------|-------------------------------------------------------------------|
| `kickoff`      | the seven-a-side kickoff fixture, the ball free on the center spot |
| `rolling-ball` | the kickoff fixture with the ball rolling at (8, 3) m/s            |
| `m0-acceptance`| all 14 players on scripted runs with target changes, rolling ball |

All scenarios use the 60 × 40 m example pitch and the default `MatchConfig`
(30 Hz). `sim-cli --list-scenarios` prints the catalog.

## The M0 acceptance scenario

`m0-acceptance` is the integration scenario of milestone M0. It starts from the
kickoff fixture with the ball rolling at (9, 4) m/s. Every player gets six
waypoints, one every six seconds, staggered by four ticks per player so the
target changes spread over two seconds. Waypoints follow a fixed arithmetic
pattern over the whole pitch, so most changes reach a player mid-run. At tick
600 player 1 is sent behind a goal line and player 14 past a corner; both targets
are moved onto the pitch. After 36 seconds everyone has reached his last waypoint.

`tests/acceptance/m0AcceptanceTests.cpp` runs it for three minutes and checks
that:

- every player covers ground and targets change while players are running,
- nine minutes of simulation never produce a non-finite value, a ball off the
  pitch, a speed above a player's limit or a target off the pitch,
- two runs produce identical checkpoint hashes, and so does playback of the
  replay after a round trip through JSON,
- advancing in batches of 1, 7, 30 or 451 ticks, each continued from a copy of
  the simulation, gives the same hashes as one uninterrupted run,
- the final state hash equals a pinned value on every platform CI builds on.

CI runs these tests in their own step (`ctest --preset ci --label-regex
acceptance`).

## Scenarios are versioned

A scenario is a fixture: changing its layout, configuration or commands changes
every match played from it and every state hash recorded against it. Replays do
not depend on the catalog — they carry their own initial state and commands — but
tests and documentation that name a scenario do. Change a scenario deliberately,
update this document in the same commit, and prefer adding a new scenario to
changing an old one.

## Adding a scenario

Add a function that returns the `MatchSetup` and an entry to the catalog in
`scenarios.cpp`, and a row to the table above. `scenariosTests.cpp` checks that
every scenario has a unique name, builds a valid setup that starts on the pitch,
and runs.
