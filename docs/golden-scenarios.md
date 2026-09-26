# Golden scenarios

The P2 golden scenarios are short, hand-placed seven-a-side situations in which
one tactical behaviour must show. Each is a [scenario](scenarios.md) in the
catalog — so it is versioned like every fixture and can be run and watched with
`sim-cli` — and `tests/acceptance/goldenScenarioTests.cpp` asserts its behaviour
through events and decision diagnostics over twenty fixed seeds. CI runs them
with the other acceptance tests; every golden scenario also has to survive a
replay round trip unchanged.

The builders live in `goldenScenarios.hpp`; their parameters let a test compare
variants of one situation. Players stand in tactic slot order — goalkeeper, two
centre backs, holding midfielder, two wingers, striker — home ids 1 to 7, away 8
to 14, on the 60 × 40 m sandbox pitch.

| Scenario          | Situation | Asserted, of 20 seeds (at the time of writing) |
|-------------------|-----------|-----------------------------------------------|
| `transition-3v2`  | Home's holding midfielder (4) wins the ball from away's (11) at the halfway line at tick 10; wingers 5, 6 and striker 7 face away's two centre backs, away's wingers and striker are caught upfield. Both sides play the reference tactic. | His first pass goes forward, at least 3 m, to one of the three attackers within 1.5 s of the regain: at least 16 (20). |
| `isolated-winger` | Home's goalkeeper plays a long ball to winger 6 on the touchline, no teammate within 10 m. Home is scripted, so nobody comes to help; away presses on `isolatedReceiver` only and stays out of its pressing phase. | Away starts a press on 6 with trigger `isolatedReceiver` and at least three players: all 20. |
| `touchline-trap`  | Away's centre back passes to its holding midfielder (11), who stands at the touchline facing his own goal. Home presses on `receiverFacingOwnGoal` with intensity 1 — presser, cover and two lane blocks. | Home's press wins the ball back within six seconds in at least 10 (13) — at least 6 more than `lone-press` (2) and than the same press in the centre (0). |
| `lone-press`      | The touchline trap at intensity 0.25: one presser, no lane blocks, no cover. | The uncoordinated comparison above. |
| `run-behind-line` | Home's holding midfielder on the ball at the halfway line; striker 7 level with away's centre backs, 20 m of space behind them. | Striker 7 chooses `runInBehind` within one second: at least 18 (20); an away defender answers with `trackRunner` on him within three seconds: at least 12 (19). |

A pass into the space behind the line is not asserted: passes go to where a
teammate stands, not into his run, until the match has a through pass. The
scenario shows the run and the defence's answer to it.

## Watching them

Record one with debug frames and open it in the [debug viewer](debug-viewer.md):

```sh
./build/debug/apps/sim-cli/sim-cli --scenario touchline-trap --seed 3 --ticks 180 \
  --replay-out trap.json --frames-out frames.json
cd apps/sim-viewer && pnpm run serve ../../frames.json
```

- **`transition-3v2`**: step to tick 11; the phases above the pitch read
  `attackingTransition` and `defensiveTransition`. Select player 4 to see his
  pass candidates and the forward pass he chooses; switch on **regions** to see
  the attackers' desired regions pushed forward.
- **`isolated-winger`**: follow the long ball to player 6; the event log shows
  `away presses #6 (isolatedReceiver, 4 players)` and the **press** overlay
  draws the presser, the lanes blocked back to the scripted defenders and the
  cover.
- **`touchline-trap`** and **`lone-press`**: compare the **press** overlay — the
  blocked lanes along the touchline against a single presser — and the event
  log's `home press ends: ballRegained`.
- **`run-behind-line`**: select player 7 to see his action candidates with
  `runInBehind` chosen, then an away centre back's `trackRunner` on him.

Or trace the decisions of a player directly:

```sh
./build/debug/apps/sim-cli/sim-cli --play trap.json --trace trace.txt --trace-players 11,4
```
