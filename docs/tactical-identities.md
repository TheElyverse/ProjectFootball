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

Whole-match statistics over all pairings, with confidence bands, are the
three-style benchmark's job.

## What this is not

The styles are as distinct as the P2 systems let them be. Carriers pass to
where a teammate stands, not into his run, and cannot dribble or hold the ball
to wait for a runner, so a counter-attack is a sequence of forward passes
rather than a run with the ball.
