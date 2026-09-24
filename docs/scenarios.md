# Scenarios

A scenario is a named, reproducible match setup: a fixture, the configuration of
the standard systems, and scripted commands. The seed is the caller's; everything
else is fixed, so a scenario name and a seed identify a match. The catalog lives
in `sim-match` (`scenarios.hpp`), and `sim-cli --scenario <name>` runs one.

| Name           | Setup                                                             |
|----------------|-------------------------------------------------------------------|
| `kickoff`      | the seven-a-side kickoff fixture, everyone at rest                 |
| `rolling-ball` | the kickoff fixture with the ball rolling at (8, 3) m/s            |
| `m0-acceptance`| all 14 players on scripted runs with target changes, rolling ball |

All scenarios use the 60 × 40 m example pitch and the default `MatchConfig`
(30 Hz). `sim-cli --list-scenarios` prints the catalog.

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
