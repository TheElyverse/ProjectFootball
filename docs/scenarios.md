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
| `pass-chain`   | home player 1 on the ball, teammates in a zigzag ahead, no opponent in reach |
| `intercepted-pass` | home player 1's only option is a risky pass past away player 8 |
| `no-passing-option` | home player 1 on the ball, every teammate behind him out of sight |

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

## The P1 passing scenarios

The P1 scenarios exercise the whole passing pipeline -- perception, pass
candidates, the seeded decision, execution, the ball's flight and reception --
without scripted runs or passes. Each is a hand-placed fixture: home players get
ids 1 to 7, away players 8 to 14, and the ball starts at player 1's feet and is
given to him at tick 0. From then on only the standard systems act.

- **`pass-chain`**: the home side stands in a zigzag up the pitch (x = 8, 20, 20,
  32, 44, 44, 54 m), everyone facing the away goal, so every carrier sees
  teammates ahead of him. The away side stands along the home goal line, behind
  every pass. Home passes the ball on from player to player.
- **`intercepted-pass`**: player 1 stands at (20, 20) facing the away goal. His
  only visible teammate is player 2, 20 m ahead at (40, 20); the others stand
  behind him, out of sight. Away player 8 waits at (34, 25.8), 5.8 m off the lane:
  close enough that the pass barely stays valid (completion just above
  `minCompletion`), far enough that player 1 plays it. Player 8 then goes after
  the ball and, for most seeds, takes it. It checks that the risk estimate means
  something: the risky passes it lets through really are lost.
- **`no-passing-option`**: player 1 stands at (30, 20) facing the away goal with
  every teammate behind him, beyond his awareness radius. He sees no one to pass
  to, decides "no valid option" every time, and keeps the ball. The away side
  waits in its own half.

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
