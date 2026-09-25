# Off-ball movement

When a side with a [tactic](tactics.md) has the ball, the carrier's teammates do
not just hold their shape: each decides what to do without the ball, with the
same pipeline a player on the ball uses to [pass](pass-decisions.md):

```text
perception → candidates and utilities → softmax choice → action → movement target
 (memory)     generateOffBallCandidates()  chooseByUtility()  PlayerAction  movement
```

The decisions live in `offBallActions.hpp`; the tactical movement system
(`tacticalMovement.hpp`) runs them together with the
[desired region](desired-region.md).

## Candidates

A teammate of the carrier weighs six actions, in this fixed order:

| Action            | Target                                                              |
|-------------------|---------------------------------------------------------------------|
| `holdPosition`    | the centre of his desired region                                     |
| `supportCarrier`  | the spot with the most open passing lane from the carrier, counting the detour from his region: on a ring `supportDistance` (10 m) around the carrier, or a third or two thirds of that from his region's centre |
| `moveIntoSpace`   | the point his team controls most, up to `spaceSearchRadius` (12 m) away, forward directions first |
| `runInBehind`     | `runDepth` (6 m) past the opponent's defensive line as he remembers it -- the second deepest opponent, the deepest being the goalkeeper -- where he is across the pitch; only if he remembers two opponents and the run is at least 3 m |
| `createWidth`     | the wing's centre line on his slot's side of the pitch, where he is along it |
| `occupyHalfspace` | the halfspace's centre line on that side                             |

Everything comes from the player's own [memory](perception.md): the opponents
he remembers, and the carrier where he believes him -- the ball itself if he does
not remember the carrier, since everyone watches the ball. Pitch control is his
team's.

## Utility

Each candidate's utility is the sum of weighted parts, `ActionScores`, kept apart
so the decision can be explained:

| Part             | Weight (default)            | Value                                        |
|------------------|-----------------------------|----------------------------------------------|
| `responsibility` | `responsibilityWeight` (1.0) | the weight of the matching responsibility in his slot: `supportCarrier`, `runInBehind`, `provideWidth`, `occupyHalfspace`; `holdResponsibility` (0.4) for holding position; 0 for space |
| `region`         | `regionWeight` (0.5)         | minus how much the target costs more than his desired region, by the [positioning cost](desired-region.md), capped at 2 |
| `space`          | `spaceWeight` (0.6)          | his team's pitch control at the target        |
| `lane`           | `laneWeight` (0.8)           | how open the passing lane from the carrier to the target is: the nearest remembered opponent's distance from it over `laneRadius` (4 m), at most 1 |
| `urgency`        | `urgencyWeight` (0.6)        | the phase's `runFrequency` for runs in behind and into space; how closely the carrier is pressed -- 1 − distance / `pressedRadius` (6 m) of the nearest opponent -- for support |
| `effort`         | `effortWeight` (0.3)         | minus the distance to the target over `effortScale` (20 m) |

`dominantScore()` names the part that contributed most, by absolute value.

## Choice and execution

`chooseByUtility()` is the seeded softmax of pass decisions, with temperature
`temperature` (0.2) and one draw from the `kAi` stream per decision. The chosen
action, its target, whom it is about and the tick are the player's
`PlayerAction`, part of his tactical state and of the state hash. He runs to its
target until he decides again; holding position follows his desired region as it
moves. When his team loses the ball the action ends.

**Relevance.** A player decides more often where it matters: within
`nearBallRadius` (20 m) of the ball every `nearIntervalTicks` (6), farther away
every `farIntervalTicks` (18) -- five and five-thirds times a second at 30 Hz.

**Diagnostics.** Every decision is reported as an `ActionDiagnostic`: all
candidates with their weighted parts and utility, and the index of the chosen
one. They are collected only on request, like pass decisions, and appear in the
debug frames as `actions`, each candidate with its `dominant` part.

`OffBallConfig` holds every parameter above and is part of `MatchConfig` and of
every replay.

## Scenarios

`tests/unit/sim-match/offBallActionsTests.cpp` checks the behavior the tactic
asks for:

- **Opening a lane.** Home's striker has the ball, pressed by an opponent four
  meters in front of him who stands right in the lane to the holding midfielder
  ten meters behind. The midfielder picks a target the presser does not screen
  -- at least 3 m from the lane -- in at least 18 of 20 seeds.
- **Keeping width.** Over ten seconds of possession the winger who provides
  width stays in the wing lane at least 90 % of the time; the same slot without
  the duty, in a narrow block, at most half of it.
- Candidates, their order and weighted parts; decision frequency by relevance;
  and actions ending with possession.
