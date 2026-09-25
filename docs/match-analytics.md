# Match analytics

`libs/sim-analytics` turns a match's [domain events](match-events.md) into
statistics. It is read-only: it never runs a system, never writes a state and
reads no state beyond the match context. Everything it measures is in the
event stream, so a match can be analysed while it runs, from a recording of its
events, or by replaying it.

## Use

```cpp
MatchAnalyzer analyzer(contextOf(setup.initialState, setup.config.ticksPerSecond));
while (simulation.tick() < end) {
  simulation.step();
  analyzer.observeStep(simulation.events());
}
const MatchStats stats = analyzer.finish(simulation.tick());
const std::string json = toStatsJson(stats);
```

The `MatchContext` holds what is fixed for a match: the pitch length, the tick
rate and which side each player plays for. `observeStep()` takes the events of
one step in order; events of players the context does not know are ignored.
`finish(tick)` closes the last possession at `tick`, the tick after the last
step.

## Metrics

Every metric is per side. Depths and thirds are measured in the side's own
frame: from its own goal line toward the one it attacks.

| Metric | From | Definition |
|---|---|---|
| `possessionShare` | `PossessionChanged` | ticks the side had the ball over ticks either side had it. A free ball — a pass in flight — belongs to the side that last owned it; the time before the first owner belongs to nobody |
| `passes`, `completedPasses`, `passCompletion` | `PassAttempted`, `PassReceived` | attempts, receptions by a teammate, and their ratio |
| `meanPassMeters` | `PassAttempted` | mean distance from `from` to `target` |
| `progressivePasses`, `completedProgressivePasses` | `PassAttempted`, `PassReceived` | passes whose target is at least `progressiveMeters` (10 m) deeper than their origin, and those received |
| `turnovers`, `regains` | `PossessionChanged` | the ball's owner changes from one side to the other; the first owner of the match is neither |
| `regainsByThird` | `PassIntercepted`, `BallWon`, `LooseBallRecovered` | regains by the third of their position; a regain without such an event in its step, a command giving the ball away for instance, is counted in `regains` only |
| `pressures`, `pressuresRegained` | `PressingStarted`, `PressingEnded` | coordinated presses and those that ended with `ballRegained` |
| `ppda` | `PassAttempted`, `BallWon`, `PassIntercepted` | passes per defensive action: opponent passes from the opponent's build-up zone — the `ppdaZone` (60 %) of the pitch nearest its own goal — over the side's won challenges and interceptions in that zone |
| `pitchControlShare` | `PitchControlSampled` | mean share of the pitch the side controls ([pitch control](pitch-control.md)) |
| `attackingThirdShare` | `PitchControlSampled` | share of samples with the ball in the side's attacking third: territory |

Rates and means are empty (`null` in JSON) where their denominator is zero:
`ppda` without a defensive action, `pitchControlShare` without a sample. The
sums `possessionShare` home + away and `turnovers` of one side = `regains` of
the other hold for every match.

`AnalyticsConfig` holds `progressiveMeters` and `ppdaZone`; the analyzer
rejects a negative or non-finite distance and a zone outside `(0, 1]`.

## JSON

`toStatsJson()` writes format `elyverse-match-stats`, version 1:

```json
{
  "format": "elyverse-match-stats",
  "version": 1,
  "ticks": 1800,
  "seconds": 60.0,
  "home": {
    "possessionShare": 0.55,
    "passes": 18,
    "...": "...",
    "regainsByThird": { "defensive": 2, "middle": 3, "attacking": 1 },
    "ppda": 4.5,
    "pitchControlShare": 0.51,
    "attackingThirdShare": 0.2
  },
  "away": { "...": "..." }
}
```

Fields appear in the order of the table above, empty values are `null` and
numbers are written in their shortest exact form. Sums are formed in event
order, so the same events always give the same bytes, on every platform.

## What this is not

No shots, goals, expected goals or player ratings yet: the match has no
shots. Pressures are team presses, not every individual pressing action.
