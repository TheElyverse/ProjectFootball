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
| `tactic-match` | the reference tactic against itself, home's forward kicks off      |
| `transition-3v2` | home wins the ball in midfield, three attackers against two defenders ([golden](golden-scenarios.md)) |
| `isolated-winger` | home plays out to an isolated winger, away presses on the trigger |
| `touchline-trap` | away's receiver faces his own goal at the touchline, home presses four players |
| `lone-press` | the touchline trap with one home presser instead of four |
| `run-behind-line` | home's striker level with away's defensive line, space behind it |

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

### Acceptance checks

`tests/acceptance/p1AcceptanceTests.cpp` runs each scenario for twenty seconds
with seed 7 and checks, on every tick, the possession invariants: a controlled
ball sits exactly where the [carry rule](possession.md) puts it and moves with
its owner, a pending pass belongs to the player on the ball, possession changes
only through a chain of `PossessionChanged` events, and passes are played by the
owner, received by teammates and intercepted by opponents. Then:

- `pass-chain`: at least three passes in a row are decided, played and received
  by home players -- with seed 7 and with each of the seeds 1 to 50;
- `intercepted-pass`: player 1's only candidate is valid with an interception
  risk above 0.5, he plays it, and player 8 intercepts it; over the seeds 1 to
  50 at least 70 % of these passes are lost, a statistical guardrail rather than
  an exact value;
- `no-passing-option`: no pass is played, player 1 keeps the ball throughout, and
  every decision finds no candidate because he sees only opponents;
- two runs and playback of the replay after a JSON round trip agree on every
  state and event hash, and collecting decision diagnostics changes neither;
- the final state and event hashes are pinned.

CI runs them with the other acceptance tests.

### Watching them

Each scenario can be run with debug frames and watched in the
[debug viewer](debug-viewer.md):

```sh
./build/debug/apps/sim-cli/sim-cli --scenario intercepted-pass --seed 7 --ticks 600 \
  --replay-out intercepted.json --frames-out frames.json
cd apps/sim-viewer && pnpm install && pnpm run build && pnpm run serve ../../frames.json
```

What to look at:

- **`pass-chain`**: click the player on the ball (yellow ring). His vision cone
  covers the teammates ahead; step to the tick of his decision (the event log
  shows the passes) to see every candidate line, the chosen one yellow, and the
  scores in the panel. Step on to watch the receiver take the ball.
- **`intercepted-pass`**: select player 1 and step to tick 19: the panel shows
  the decision he made in the step from tick 18, one valid pass to player 2 with
  a risk of about 0.64. After the pass, select player 8 to follow his run to the
  ball; the event log shows the interception at tick 70.
- **`no-passing-option`**: select player 1. His vision cone points at the away
  side, his teammates are behind him with no observation circle, and every
  decision reads "no valid option".

## The tactic match

`tactic-match` is the fixture two tactics play on: the seven-a-side kickoff
fixture with a tactic on each side, the standard configuration, and
`GiveBall` to home's forward (player 7) at tick 0. `makeTacticMatch()` builds it
for any two tactics that fit seven a side; the catalog entry uses the reference
tactic on both sides. The CLI swaps in tactic files:

```sh
./build/debug/apps/sim-cli/sim-cli --home-tactic data/tactics/pressing.json \
  --away-tactic data/tactics/counter.json --seed 3 --ticks 1800 --frames-out frames.json
```

`--home-tactic` and `--away-tactic` imply `--scenario tactic-match`; a side
without one plays the reference tactic. The replay records the tactics
themselves, so it plays back without the files. See
[tactical identities](tactical-identities.md) for the presets.

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
