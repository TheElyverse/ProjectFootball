# Tactical identities

Three tactics under `data/tactics/` give the sandbox recognisable styles:
`possession`, `counter` and `pressing`. They use the same seven-a-side tactic
model as `reference.json` (see [tactics](tactics.md) and the
[file format](tactic-format.md)) and differ **only in data**: no system knows
their names, and a new style is a new file.

## Intent and parameters

| | possession | counter | pressing |
|---|---|---|---|
| Intent | keep the ball, circulate safely, win it back at once after losing it | defend deep and compact, strike quickly and vertically after a regain | hunt the ball high, with several players, on every trigger |
| Shape | wide 1-2-1-3, wingers on the touchlines | narrow 1-2-1-2-1 with inside forwards | high 1-2-1-3 with a central midfielder |
| `pressingLine` | 0.55 | 0.85 (rarely presses) | 0.4 (presses early) |
| Pressing triggers | poor first touch, back pass | none | all five |
| `lineHeight` out of possession | 0.35–0.4 | 0.15 | 0.4–0.45 |
| `pressingIntensity` | 0.4, 1.0 in defensive transition (counterpress) | 0.1–0.25 | 1.0 |
| `passingRisk` | 0.2–0.35 | 0.6, 0.9 in attacking transition | 0.5–0.75 |
| `runFrequency` | 0.2–0.35 | 0.5–0.9 | 0.3–0.7 |
| `blockWidth` in possession | 0.9–0.95 | 0.8 | 0.85 |

The positioning weights follow the same intent: possession values spacing
and occupancy and guards against transitions (`transitionRisk` 0.8), counter
cares least about occupancy and transition risk and most about reaching its
targets.

`passingRisk` reaches the player on the ball through `scoringForRisk()`
([pass decisions](pass-decisions.md)): the higher it is, the more a pass is
worth for the ground it gains and the less its interception risk counts.

## Playing them against each other

Any two tactics that fit seven a side meet in the tactic match
([scenarios](scenarios.md)):

```sh
./build/debug/apps/sim-cli/sim-cli --home-tactic data/tactics/pressing.json \
  --away-tactic data/tactics/counter.json --seed 3 --ticks 1800 --frames-out frames.json
```

In code, `makeTacticMatch({.home = ..., .away = ...}, seed)` builds the same
setup.

## Signatures

`tests/acceptance/tacticalIdentityTests.cpp` checks what makes each style
recognisable, over fixed seeds:

- **Pressing wins the ball back higher.** Over twenty one-minute tactic
  matches against the reference tactic, pressing regains the ball more often
  than counter and at least one and a half times as often in the opponent's
  half (at the time of writing 108 regains, 62 of them high, against 67 and
  32).
- **Counter plays forward after a regain.** A hand-placed moment: home's
  holding midfielder has just won the ball in midfield, the striker is ahead
  of him with a defender near the lane, the centre backs behind him are free.
  In its attacking transition, counter's first pass goes more than five
  metres forward in at least eight more of twenty seeds than possession's
  does (at the time of writing 18 against 5).

`tacticFilesTests.cpp` additionally pins the direction in which the files
differ, such as pressing's lower pressing line and counter's deeper block,
so an edit cannot quietly flatten an identity.

## Over whole matches

The [style benchmark](sim-benchmark.md#a-round-robin-of-styles) plays every
pairing of the three identities. The full run — twenty 90-minute matches per
pairing, seed 1, restarts on, core 0.18.0, 180 matches — gives, per style over
its 120 matches home and away, mean and 95 % interval:

| Metric                  | possession            | counter              | pressing               |
|-------------------------|-----------------------|----------------------|------------------------|
| possession share        | 0.50 [0.47, 0.53]     | 0.44 [0.39, 0.48]    | **0.57** [0.55, 0.58]  |
| passes                  | 1052 [986, 1118]      | **626** [551, 701]   | 1396 [1341, 1450]      |
| pass completion         | 0.67 [0.66, 0.68]     | 0.68 [0.67, 0.70]    | **0.63** [0.62, 0.63]  |
| mean pass length (m)    | **12.4** [12.3, 12.5] | 12.0 [11.8, 12.1]    | 11.5 [11.4, 11.5]      |
| progressive passes      | 157 [144, 169]        | **77** [67, 87]      | 192 [180, 204]         |
| regains                 | 387 [357, 417]        | **228** [196, 261]   | **557** [531, 584]     |
| regains, attacking third| 89 [83, 95]           | 42 [37, 47]          | **179** [171, 188]     |
| presses                 | 379 [358, 400]        | **0**                | **1083** [1040, 1126]  |
| PPDA                    | 2.88 [2.80, 2.96]     | **3.95** [3.76, 4.13]| **2.36** [2.32, 2.40]  |
| pitch control share     | 0.52 [0.52, 0.53]     | **0.45** [0.44, 0.47]| 0.52 [0.51, 0.53]      |
| ball in attacking third | 0.29 [0.26, 0.33]     | 0.33 [0.28, 0.38]    | **0.40** [0.37, 0.43]  |

Every pair of styles separates — non-overlapping intervals — on at least
twelve metrics. What each identity shows:

- **Pressing** presses about three times as often as possession, wins the ball
  back most often and twice as often as possession in the attacking third,
  allows the fewest passes per defensive action, and — winning it high — has
  the most of the ball and the ball most in the attacking third. It pays with
  the lowest pass completion.
- **Counter** never presses, allows the most passes per defensive action,
  controls the least of the pitch and plays the fewest passes; the ball still
  spends as much time in its attacking third as in possession's.
- **Possession** plays the longest and a safe passing game, and presses
  moderately through its counterpress.

Two things are not there yet: possession does not own the most of the ball —
the pressing side wins it back so often that it has more — and counter's
directness shows in few, completed passes rather than in fast progression,
since players cannot dribble or play a through pass into a run. Both depend on
match mechanics beyond P2, not on the tactic files.

## What this is not

The styles are as distinct as the P2 systems let them be. Carriers pass to
where a teammate stands, not into his run, and cannot dribble or hold the ball
to wait for a runner, so a counter-attack is a sequence of forward passes
rather than a run with the ball.
