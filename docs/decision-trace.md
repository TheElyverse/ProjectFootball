# Decision trace

The decision trace answers "why did he do that, and why did it go wrong?" for a
recorded match. It replays the match with [diagnostics](match-events.md#decision-diagnostics)
on, lists every decision of the chosen players in the chosen ticks with the
reasons behind it, and follows every pass to its outcome. Tracing changes
nothing: the playback verifies every checkpoint as usual.

## From the command line

```sh
./build/debug/apps/sim-cli/sim-cli --scenario intercepted-pass --seed 7 --ticks 150 \
  --replay-out replay.json
./build/debug/apps/sim-cli/sim-cli --play replay.json --trace trace.txt \
  --trace-players 1,8 --trace-from 0 --trace-to 120
```

`--trace` writes the trace; `--trace-players` (comma-separated ids),
`--trace-from` and `--trace-to` (inclusive ticks) narrow it. Without them every
decision of the match is traced. A trace line reads

```text
t=18 #1 passes to #2 (utility 0.42: completion +0.36 progression +0.26 pressure -0.00 risk -0.19; estimated risk 0.64; 1 options, 9 observed) because completion -> intercepted by #8 at t=70: decision
t=66 #4 pressCarrier #14 (utility 0.97: responsibility +0.60 region -0.47 space +0.19 lane +0.00 urgency +0.68 effort -0.03; 5 options) because urgency
t=81 #5 blockLane #12 (assigned by the team press)
```

- **Observations**: how many entities the player remembered when he decided.
- **Candidates**: how many options he had, and the chosen one's utility split
  into its weighted parts — `passContributions()` for a pass
  ([pass candidates](pass-candidates.md)), `ActionScores` for an action without
  the ball ([off-ball movement](off-ball-movement.md)).
- **Reason**: the part with the largest absolute weight (`dominantContribution()`,
  `dominantScore()`).
- **Outcome** of a pass: `received`, `intercepted`, `recovered by the passer`,
  `not played` (he lost the ball first) or `pending` at the end of the trace.

## Why a pass failed

An intercepted pass is attributed to one stage of the decision pipeline
(`attributeInterception()`), checked in this order:

| Cause | When |
|---|---|
| `perception` | the passer had not seen the interceptor — no observation of at least the scoring's `minConfidence` — or believed him more than `perceptionErrorMeters` (3 m) from where he really was when he decided |
| `decision` | he saw him and still chose a pass with an estimated interception risk of at least `riskyDecision` (0.5) |
| `execution` | he saw him and judged the pass safe: the kick itself — direction and speed error, pressure on the passer — let it fail |

The limits are `TraceConfig`; they classify, they do not change the match.

## In code

```cpp
DecisionTracer tracer(replay.setup.config);
const auto playback = playReplay(
    replay, {.diagnostics = DiagnosticsFilter{.players = {PlayerId(4)}, .from = {}, .to = {}},
             .afterStep = [&tracer](const MatchSimulation& simulation) {
               tracer.recordStep(simulation);
             }});
const std::string text = formatTrace(tracer.entries());
```

`DecisionTracer` works on any simulation with diagnostics on, not only on a
playback. Its entries hold the full diagnostics, for tools that want more
than the text.

## Cost

Diagnostics are built only for the filtered players in the filtered ticks; the
rest of the match runs at full speed. With diagnostics off — every normal run
— a decision pays one pointer check.
