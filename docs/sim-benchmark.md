# sim-benchmark

`apps/sim-benchmark` plays a series of seven-a-side matches between two tactic
files and writes their statistics: how each style fares against another over
many matches, with confidence intervals instead of anecdotes. It is also the
performance check of the match simulation.

```sh
./build/release/apps/sim-benchmark/sim-benchmark \
  --home data/tactics/pressing.json --away data/tactics/counter.json \
  --matches 20 --minutes 90 --seed 1 --jobs 4 --out pressing-counter.json
```

| Option                        | Default | Meaning                                              |
|-------------------------------|---------|------------------------------------------------------|
| `--home`, `--away`            |         | the two tactic files, required                       |
| `--matches`                   | 10      | how many matches                                     |
| `--seed`                      | 1       | the base seed of the series                          |
| `--minutes`                   | 10      | match length in simulated minutes                    |
| `--jobs`                      | 1       | matches played at once                               |
| `--out`                       |         | where to write the results file                      |
| `--no-restarts`               |         | leave the ball on the line when it goes out          |
| `--max-seconds-per-match`     |         | fail if a match takes longer, in wall-clock seconds  |

## A round robin of styles

With `--style` instead of `--home` and `--away`, repeated once per tactic file,
the benchmark plays every ordered pairing of the styles — each against itself
too — as a series of `--matches` matches. Pairing `k` (home style by home
style: (0, 0), (0, 1), …) plays with base seed `matchSeed(seed, k)`, so pairings
play unrelated matches.

```sh
./build/release/apps/sim-benchmark/sim-benchmark \
  --style data/tactics/possession.json --style data/tactics/counter.json \
  --style data/tactics/pressing.json \
  --matches 20 --minutes 90 --seed 1 --jobs 2 --verify-replays 1 --out styles.json
```

The results file, format `elyverse-style-benchmark`, version 1, holds the
styles, each style's `styleSummary` over all its matches — home and away —
the `separations`, and every pairing's summaries and matches. A **separation**
is a metric on which one style's 95 % interval lies wholly above another's: a
difference beyond chance. The console lists them.

`--verify-replays n` records the first `n` matches of every series again as a
replay, plays it back and fails the run if a checkpoint differs: the sampled
determinism check.

## The matches

Every match is the [tactic match](scenarios.md#the-tactic-match): the kickoff
fixture with the two tactics, the standard systems and
[restarts](restarts.md) on — over whole matches a ball waiting on the line would
blur every statistic. Match `i` plays with seed `matchSeed(base, i)`, SplitMix64
of the base seed advanced by `i + 1` steps: neighbouring matches get unrelated
seeds, and a match's seed depends only on the base seed and its index, never on
how many matches run or in which order. Any match's tactics and seed can be
replayed with `sim-cli --home-tactic ... --away-tactic ... --seed <its seed>`,
but `sim-cli` has no restart option and defaults to `RestartConfig::enabled ==
false`, so a match where the ball goes out of play diverges from `sim-cli`'s
replay of it once it does.

Statistics come from [match analytics](match-analytics.md) over each match's
events. A match fails the whole run — naming its index, seed and tick — if a
step fails or produces a non-finite value, or a statistic is not a finite
number.

## Results

The results file is JSON, format `elyverse-benchmark`, version 1: the core
version, both tactics by name and content hash, the series (`matches`,
`baseSeed`, `ticks`, `restarts`), a `summary` per side and metric, and
`results`, every match's index, seed and statistics.

A metric's summary holds its `count` of matches — metrics a match leaves empty,
such as a PPDA without defensive actions, are left out — its `mean`, sample
`variance` and `ci95`, the 95 % confidence interval of the mean,
`mean ± t · s / √n` with Student's t (`seriesSummary.hpp`).

The file holds no timing, and every sum runs in match order. So the same
tactics, series and core version always give **the same bytes**, whatever
`--jobs` is; a test and the benchmark smoke test hold that.

The console shows each match's seed and wall-clock time, the headline metrics
of both sides with their intervals, and the mean and slowest match time.

## P2 acceptance

**In CI**, the `style-benchmark` job builds the `ci-release` preset and runs
`ctest -L benchmark`: `tests/benchmark/p2StyleBenchmark.cmake` plays the round
robin of the three [tactical identities](tactical-identities.md), six
six-minute matches per pairing with seed 1 and one sampled replay per pairing.
It must finish without an invalid value within 5 s per match and 120 s for
all 54 (about 20 s at the time of writing), every sampled replay must
reproduce, and the styles must separate where their identities promise:

| Metric                  | Separation                              |
|-------------------------|-----------------------------------------|
| `regains`               | pressing above possession and counter   |
| `regainsAttackingThird` | pressing above possession and counter   |
| `ppda`                  | possession and counter above pressing — pressing allows the fewest passes per defensive action |

These hold for base seeds 1, 2 and 3 of the reduced run. The test is
registered for Release builds only; in Debug it would take minutes.

**The full run** is twenty 90-minute matches per pairing, the command above.
Its results at the time of writing (core 0.18.0) are in
[tactical identities](tactical-identities.md#over-whole-matches).

## P2 performance budget

A 90-minute seven-a-side match with the standard systems, restarts on and two
P2 tactics must simulate in **under 10 seconds** of wall-clock time on one core
of a CI-class machine in a Release build. At the time of writing a
pressing-against-counter match takes about 5.8 s (2-core cloud container,
GCC 13, Release): enough headroom for the P3 systems to come without
revisiting it yet. `--max-seconds-per-match 10` checks it.

Debug builds are around ten times slower; the smoke test therefore plays two
one-minute matches only.
