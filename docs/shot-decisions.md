# Shot decisions

A player on the ball weighs shooting against passing. He looks at the goal he
attacks, splits it into zones, judges for each what his shot would become —
blocked, saved, a goal, a second ball — and offers his best shot to the same
[choice](pass-decisions.md) his passes compete in. The decision records where he
aims; how well he strikes it is execution's business. Candidate generation lives
in `sim-match` (`shotCandidates.hpp`) as the pure function
`generateShotCandidates(state, shooterIndex, now, secondsPerTick, rules)`; the
choice and the pending shot live in the decision system of
[pass decisions](pass-decisions.md).

```text
perception → shot candidates      → one softmax with the passes → ShotIntent  → strike
 (memory)     generateShotCandidates()  chooseOnBall()             pendingAction  ball system
```

## Only what he sees

Like the [pass candidates](pass-candidates.md), shot candidates read only the
shooter's [perception](perception.md) memory:

- **Opponents** count only if he remembers them with at least `minConfidence`
  (0.3), at their estimated positions and last seen velocities, imagined with
  the default movement limits. An opponent he has not seen neither blocks nor
  saves.
- **The keeper** is the opponent whose slot in his side's tactic guards the goal
  (`isGoalkeeper()`): who keeps goal is known from the squad, where he stands
  is not. A side without a tactic has no keeper, and a keeper the shooter does
  not remember saves nothing.
- **His own accuracy** he knows: `PlayerAttributes::shotAccuracy`, from 0
  (wild) to 1 (pinpoint), 0.5 by default ([match state](match-state.md)).

## Where he can shoot from

Three things rule out every shot at once, before anything is scored:

| Rejection    | When                                                                   |
|--------------|------------------------------------------------------------------------|
| `kTooFar`    | the ball is farther than `maxShotDistance` (25 m) from the goal's center |
| `kGoalUnseen`| the goal's center is outside his field of view: he is facing away      |
| `kTooNarrow` | the goal looks narrower than `minOpening` (0.1)                        |

A player facing away from the goal would have forgotten the keeper within
seconds and judged every shot an open goal; turning to shoot comes with the
on-ball actions. The **opening** is how wide the goal looks: the distance
between the directions to its two posts as unit vectors, `2·sin(angle / 2)`
(`goalOpening()`) — about the angle in radians for a narrow goal, 2 on the goal
line between the posts, computed without trigonometry so that it is the same on
every platform.

## The aiming grid

The goal's mouth, between the posts and up to the crossbar, is split into
`zoneColumns` × `zoneRows` (7 × 5) zones. Each zone is a candidate, aimed at a
point on the goal line and a height:

- an inner zone at its center,
- a zone on the frame — the outer columns and the top row — at the point as
  close to the post or crossbar as his spread lets the shot stay inside, but
  never closer than `frameMargin` (0.1 m) and never beyond the zone's center.

So a pinpoint finisher aims a tenth of a meter inside the post; an average one
keeps his spread inside the frame; a wild one aims at the zone's center.

## The spread

A shot ends up somewhere around its aim: a triangular spread, independent across
and up, of half-width

```text
spread = (spreadAtZero + spreadPerMeter · distance)
       · (1 + pressureSpread · pressure)
       · (0.1 + 1.8 · (1 − shotAccuracy))
```

with `distance` to the goal's center and `pressure = max(0, 1 − d / pressureRadius)`
for the nearest remembered opponent at `d`. An average finisher (0.5) spreads
0.2 m plus 4 cm per meter; a pinpoint one a tenth of that, a wild one almost
twice. The triangle's distribution function is quadratic, so the share of the
spread in each zone is exact arithmetic. A shot aimed low skids along the grass:
the lowest row takes everything below its top.

## What becomes of a shot at a zone

For each zone's center, the shot is a straight line from the ball, rising from
the ground to the zone's height at the goal line, struck at `shotSpeed` (25 m/s)
and slowing like a rolling ball. As for passes, every half meter along it the
shooter compares when the ball gets there with when a player can
([pass candidates](pass-candidates.md)):

- **A blocker** — every remembered opponent but the keeper — gets a body part to
  the ball where it is no higher than `blockReach` (1.8 m). Standing within the
  control radius of the ball's path he needs no time at all; farther away, the
  run until he is.
- **The keeper** reaches the ball within an ellipse: `keeperDiveReach` (2 m) to
  either side at his feet, `keeperJumpReach` (2.6 m) straight up, narrower in
  between. Within it he needs `keeperDiveSeconds` (0.5 s) times the share of his
  reach he stretches; beyond it, the run until the ball is in reach plus the full
  dive.

A player's margin is the least time he has to spare. His chance to get there
falls smoothly from 1 at `−margin` through ½ at 0 to 0 at `+margin` — the cubic
smoothstep, `blockMarginSeconds` (0.15 s) wide for a blocker, who only has to
stand in the way, and `saveMarginSeconds` (0.4 s) for the keeper, whose save is
never certain — weighted by how sure the shooter is of him. The shot survives
every blocker, `unblocked = Π (1 − block)`, and then the keeper:

```text
block = 1 − unblocked
save  = unblocked · keeperRisk
goal  = unblocked · (1 − keeperRisk)
```

## A candidate's chances

Weighting every zone's outcome by the share of the spread that ends up there
gives the candidate's chances:

| Component          | How                                                                    |
|--------------------|------------------------------------------------------------------------|
| `onTarget`         | the share of the spread inside the frame                               |
| `blockRisk`        | block over the spread — a wide shot passes the zone on the frame nearest it, so a blocker in front of the goal blocks it too |
| `saveRisk`         | save over the spread inside the frame                                  |
| `goalChance`       | goal over the spread inside the frame                                  |
| `secondBallChance` | `reboundShare` (0.3) of `blockRisk + saveRisk`: a rebound or a corner  |
| `utility`          | `goal·w_g + secondBall·w_s − loss·w_k`                                 |

where `loss = 1 − goalChance − secondBallChance` is the chance his team loses the
ball. `shotContributions(candidate, scoring)` splits the utility into `goal`,
`secondBall` and `possession`, whose sum is the utility bit for bit;
`dominantShotContribution()` names the largest, the reason a
[decision trace](decision-trace.md) gives.

**Keeping the ball** is the alternative a shot is measured against: it is worth
0, so a shot is only offered at a positive utility. `w_k` (1.0) matches what a
completed pass is worth (`completionWeight`), so losing the ball costs as much as
keeping it is worth. An explicit option to hold the ball arrives with the other
on-ball actions.

## Valid candidates

Besides the three rejections of the shooter's position, a candidate is rejected
when

| Rejection     | When                                                     |
|---------------|----------------------------------------------------------|
| `kBlocked`    | `blockRisk` is at least `maxBlockRisk` (0.7)             |
| `kUnlikely`   | `goalChance` is below `minGoalChance` (0.05)             |
| `kNotWorthIt` | the utility is 0 or less: keeping the ball is worth more |

Every zone stays in the list with its reason, so a decision can be explained.
Valid candidates come first, by descending utility, then the rest; ties by
column, then row.

## The choice

`chooseOnBall()` offers the valid passes and the **best** valid shot — one shot,
not every zone of the goal, or a player with seven good zones would shoot seven
times as often — to the seeded softmax of [pass decisions](pass-decisions.md),
with the same temperature and exactly one draw from the `kAi` stream per
decision. A side with a tactic scales the shot scoring by the `passingRisk` `r`
of its current phase, as it does the pass scoring (`shotScoringForRisk()`):

| Field           | Scaled by               | Effect of a bold tactic (`r` near 1) |
|-----------------|-------------------------|--------------------------------------|
| `goalWeight`    | `0.5 + r`               | values a goal more                   |
| `lossWeight`    | `1.5 - r`               | fears losing the ball less           |
| `minGoalChance` | `1.3 - 0.6 r`, at most 1 | offers less likely shots too        |

At `r = 0.5` nothing changes.

## The shot

A chosen shot becomes the shooter's pending action, a `ShotIntent` with the aimed
point, height and speed ([match state](match-state.md)). The ball system strikes
it in the next step, if he still has the ball, records `ShotAttempted` — shooter,
where from, the aimed point and height, the speed, the distance and the opening —
and the state's `lastShot()`. A ball won back from a shot is loose, not a pass
received or intercepted ([match events](match-events.md)).

Until shot execution exists, the strike is a placeholder: the ball is kicked
along the ground at the aimed point on the goal line with the pass execution's
error, at `shotSpeed` even where that is harder than the passes' `maxSpeed`, and
the aimed height goes unused. There are no goals yet either: a ball
over the goal line is a goal kick or a corner ([restarts](restarts.md)).

## Scenarios and guardrails

The `clear-chance`, `hopeless-angle` and `blocked-lane` [scenarios](scenarios.md)
check the decision over 100 seeds: an eight-meter chance with only the keeper to
beat is taken at least 90 times, a goal seen from the goal line six meters wide of
the post is `too narrow` every time and the free teammate gets the ball, and with
a defender two meters in front the player passes more often than he shoots. The
reduced P2 [style benchmark](sim-benchmark.md) requires every side to shoot 1 to
40 times per six-minute match.

At the time of writing, an average finisher facing a keeper on his line judges
his best shot from straight in front of the goal at a goal chance of about 0.67
from 8 m, 0.41 from 12 m, 0.09 from 18 m and 0.06 from 22 m. In the six-minute
style benchmark a side shoots 2 to 27 times, pressing the most: it wins the ball
high up the pitch. That is more than real football — the sandbox has no goals,
so a shot that goes in becomes a goal kick that the pressing side wins back near
the goal, and no keeper positioning, so keepers drift off their line — and the
balance will be refined with them.

## Configuration

`ShotScoringConfig` is the `shooting` part of the decision configuration
(`DecisionConfig`, part of `MatchConfig` and of every replay):

| Field                | Default | Meaning                                                  |
|----------------------|---------|----------------------------------------------------------|
| `minConfidence`      | 0.3     | observations below are ignored                           |
| `maxShotDistance`    | 25 m    | farther from the goal's center, no shot                  |
| `minOpening`         | 0.1     | a narrower goal offers no shot                           |
| `zoneColumns`        | 7       | the aiming grid across the goal, at least 2              |
| `zoneRows`           | 5       | the aiming grid up the goal, at least 1                  |
| `shotSpeed`          | 25 m/s  | how fast a shot leaves the foot                          |
| `keeperDiveReach`    | 2 m     | the keeper's reach to either side at his feet            |
| `keeperJumpReach`    | 2.6 m   | the keeper's reach straight up                           |
| `keeperDiveSeconds`  | 0.5 s   | reaction and dive to the edge of his reach               |
| `blockReach`         | 1.8 m   | an outfield player blocks below this height              |
| `blockMarginSeconds` | 0.15 s  | width of a blocker's risk step                           |
| `saveMarginSeconds`  | 0.4 s   | width of the keeper's risk step                          |
| `spreadAtZero`       | 0.2 m   | an average finisher's spread at the goal                 |
| `spreadPerMeter`     | 0.04    | and how much it grows per meter of distance              |
| `pressureRadius`     | 3 m     | reach of an opponent's pressure on the shooter           |
| `pressureSpread`     | 1.0     | how much full pressure widens the spread                 |
| `frameMargin`        | 0.1 m   | the closest a shot is aimed to a post or the crossbar    |
| `reboundShare`       | 0.3     | blocked and saved shots his team gets the ball back from |
| `maxBlockRisk`       | 0.7     | more likely blocked, not offered                         |
| `minGoalChance`      | 0.05    | less likely to go in, not offered                        |
| `goalWeight`         | 2.5     | `w_g`                                                    |
| `secondBallWeight`   | 0.5     | `w_s`                                                    |
| `lossWeight`         | 1.0     | `w_k`                                                    |

`validate(ShotScoringConfig)` rejects values outside these rules; see
`shotCandidates.hpp`.

## What this is not

Shots are struck along the ground and nothing becomes of them but a ball on its
way: no flight to the aimed height, no goal, save or deflection, no goal events —
that is shot execution. Keepers do not position themselves. How well a player
decides and how badly he misjudges his chances do not depend on him yet; every
player chooses with the same temperature. He cannot turn toward the goal, carry
the ball into a better position or hold it on purpose; those are the on-ball
actions still to come.
