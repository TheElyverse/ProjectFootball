# Goalkeeper distribution

A goalkeeper with the ball restarts his side's play his own way: he throws it
out of his hands, punts it, plays out with his feet or kicks it long, and he
takes the goal kick (GDD section 8.8). He decides like every player on the
ball ([pass decisions](pass-decisions.md)) — the same pipeline, the same
softmax, the same `kAi` stream — from a list of options that holds his own
next to the ground passes everyone has. How much he plays out and how often
he goes long is his tactic's call. The model lives in `sim-match`
(`distribution.hpp`); the [ball system](ball-movement.md) carries it out and
the [restart system](restarts.md) places the goal kick.

```text
ball in his hands or at his feet
  → options: ground passes (from his hands: throws, at most throwRange)
             + long balls: generateLongBallCandidates(), lofted
               utility + loftedBias = directnessBias(tactic's directness)
  → chooseOnBall() (kAi)
  → PassIntent{lofted}  → ball system: executePass() with loftedPassConfig() (kExecution)
                                       + loftedRise(): up on the flight model
```

## The ball in his hands

A keeper who takes the ball with his hands ([goalkeeper](goalkeeper.md#hands),
a caught shot, a high ball he holds in the air) **holds** it: the ball's
`held` is true. Holding it

- he stands where he took it: his movement target is gone;
- once he no longer runs he looks up the pitch, where his options are, not
  at the ball in his hands -- he would otherwise keep looking wherever he
  took it, at his own goal after a ball rolled back to him, see no one to
  play to and keep the ball for good;
- nobody challenges him for it ([pressing](pressing.md));
- he keeps it at least `holdSeconds` (2 s) from the moment he took it, for
  his side to get into shape, against the half second (`minHoldSeconds`)
  anyone else keeps a ball he has just taken;
- his ground passes are **throws**, rolled out along the ground like a pass
  and no farther than `throwRange` (25 m);
- his lofted passes are **punts**.

Whatever he plays, the ball leaves his hands: his tactical state records the
tick (`handsReleased`), and **he may not take it in them again** until
another player has touched it (`hasReleasedBall()`). Until then he reaches it
as an outfield player does, with his feet, in his area as outside it; the
ball system forgets the release as soon as anyone else is the ball's last
touch. A restart forgets it too.

## Long balls

Only a goalkeeper kicks long. `generateLongBallCandidates()` lists a lofted
pass to every teammate he remembers with at least the scoring's
`minConfidence`, aimed at where he believes that teammate is now, from his
memory alone like every pass candidate:

- **The kick.** The ball comes down on the target after `flightSeconds` (3 s):
  its speed along the ground is `flightSpeed()` — the closed form of the
  flight under drag, `distance / (seconds · φ₁(drag · seconds))` — and its
  vertical speed `launchVerticalVelocity()` for that speed and distance
  ([ball movement](ball-movement.md#flight)). Over 25 to 50 m the ball goes
  some ten meters up and comes down steeply, onto heads rather than feet.
- **The contest.** Where it comes down, the receiver and every remembered
  opponent race for it, each counted from no earlier than the ball arrives:
  an opponent's margin is `max(his time, flight) − max(receiver's time,
  flight)`. Two players who are both under the ball in time are even. His
  risk falls smoothly from 1 at a margin of −`contestMarginSeconds` (1 s)
  through ½ at 0 to 0 at +1 s, weighted by how sure the keeper is of him —
  the step of the [pass candidates](pass-candidates.md#scores) — and the ball
  survives every opponent: `interceptionRisk = 1 − Π (1 − risk)`.
- **The scores** are those of a ground pass otherwise
  (`scorePassCandidate()`): completion, progression and the receiver's
  pressure.
- **Rejections.** A long ball shorter than `minLongDistance` (25 m) is
  `kTooClose`, one that needs more than the passing's `maxLoftedSpeed`
  (28 m/s along the ground, about 53 m) `kOutOfReach`, one less likely than
  `minCompletion` `kUnlikely`.

The long balls join his ground passes in one list, ordered as
`orderPassCandidates()` orders every pass list; the choice among them is the
carrier's softmax.

Long distribution is contested because it flies: the ball system plays it on
the [flight model](ball-movement.md#flight), and whoever gets to it where it
comes down wins it in the air ([aerial duels](aerial-duels.md)) or at his
feet ([reception](reception.md)).

## The tactic's dial

`principles.goalkeeper.directness` in [0, 1] ([tactics](tactics.md)) says how
readily he goes long rather than playing out. It acts on the utility of his
long balls alone, as a part of their own:

```text
loftedBias = directnessWeight · (2 · directness − 1)
```

With the default `directnessWeight` of 0.5, a keeper of directness 0 finds
every long ball half a point worse than its merits, one of 1 half a point
better, and one of 0.5 weighs both on their merits: nothing is hard-coded but
the scores. The presets: possession 0.15, reference 0.5, pressing 0.6,
counter 0.85. A side without a tactic has no keeper and no long balls.

`passContributions()` reports the bias as `directness` (0 for every ground
pass), and the [decision trace](decision-trace.md) names it the reason when it
outweighs the rest.

## Execution

A lofted `PassIntent` (`lofted` true) is executed like a pass, with
`loftedPassConfig()`: the hardest kick `maxLoftedSpeed` instead of
`maxSpeed`, and both execution errors `loftedErrorFactor` (2) times as large,
grown by pressure as a pass's are. The ball then goes up at the vertical speed
that brings it down on the target at the intended speed, scaled by the same
pace error as its speed along the ground (`loftedRise()`) — 45 degrees should
the drag stop such a flight short. The same two `kExecution` draws as any
pass; no spin. A punt leaves the hands at the keeper's feet: there is no drop
kick height.

## The goal kick

A goal kick goes to the defending side's goalkeeper
([restarts](restarts.md)). He takes it from his goal area: the ball lies on
`goalKickSpot()`, the front edge of the goal area level with where the ball
went out, but no wider than the goal area; he stands `carryDistance` behind it
facing up the pitch, at rest. He has it at his feet and decides like after
any other restart: ground passes or a long kick.

## Diagnostics

A goalkeeper's decision is a `DecisionDiagnostic` like everyone's: every
option with its scores, lofted or not, and his choice, with `held` saying
whether the ball was in his hands. The [decision trace](decision-trace.md)
names what he did — `throws to`, `punts to`, `kicks long to` or
`passes to` — with the utility's parts and the reason:

```text
t=0 #1 throws to #2 (utility 1.11: completion +1.00 progression +0.11 pressure -0.00 risk -0.00; estimated risk 0.00; 8 options, 13 observed) because completion -> received by #2 at t=58
t=0 #1 kicks long to #5 (utility 1.24: completion +0.86 progression +0.42 pressure -0.00 risk -0.04 directness +0.00; estimated risk 0.14; 12 options, 13 observed) because completion -> pending
```

## Configuration

`DistributionConfig` is the `distribution` part of `DecisionConfig`
([pass decisions](pass-decisions.md)), and so of `MatchConfig` and every
replay:

| Field                  | Default | Meaning                                                     |
|------------------------|---------|-------------------------------------------------------------|
| `holdSeconds`          | 2 s     | he keeps a ball in his hands at least this long             |
| `throwRange`           | 25 m    | the farthest he throws                                      |
| `minLongDistance`      | 25 m    | the shortest long ball                                      |
| `flightSeconds`        | 3 s     | how long a long ball is in the air                          |
| `contestMarginSeconds` | 1 s     | width of the contest-risk step where a long ball comes down |
| `directnessWeight`     | 0.5     | the lofted bias at directness 1, and minus it at 0          |

`validate()` rejects a non-finite value, a negative hold, shortest long ball or
weight, a weight above `kMaxScoringWeight`, and a throw range, flight time or
margin that is not positive. The kick's own limits are `PassConfig`'s
`maxLoftedSpeed` (28 m/s) and `loftedErrorFactor` (2)
([passing](passing.md)); the bias is the scoring's `loftedBias`
([pass candidates](pass-candidates.md)), which the decision system sets for a
keeper and which is 0 in the configuration.

## Scenarios and tests

| Scenario           | Setup | Checked |
|--------------------|-------|---------|
| `keeper-build-up`  | Home's keeper holds the ball at his goal, his tactic's directness 0.1; his centre backs stand wide and free; away, without a tactic, stands a meter in front of his holding midfielder, in the lane from the keeper, and a meter off his wingers and striker. | Over ten seeds he throws, along the ground, to a centre back, who receives it; his hands are barred from the throw until the centre back has it. |
| `keeper-turned`    | `keeper-build-up` with the keeper facing his own goal, every option behind him. | Over ten seeds he turns up the pitch and throws to a centre back within five seconds. |
| `keeper-long-kick` | Home's keeper has the ball at his feet, directness 0.5; away stands in the lanes to his centre backs and his holding midfielder, a forward four meters to his side, out of the way of a long ball, its last two defenders deep; his wingers and striker wait beyond the longest ground pass. | Over ten seeds he kicks long to a winger or the striker, the ball goes more than five meters up, and somebody gets to it. |
| `goal-kick`        | Home player 1 plays the ball wide over away's goal line; restarts are on; away plays the reference tactic. | Over ten seeds away's keeper takes the goal kick, from `goalKickSpot()` in his goal area, the ball at his feet, and plays it from there. |

`tests/acceptance/distributionScenarioTests.cpp` holds the scenario checks;
`tests/unit/sim-match/distributionTests.cpp` that a long ball comes down on its
target after the flight time, that the dial biases long balls either way, the
rejections, that an opponent under a long ball in time makes it a fifty-fifty
and one far off does not, and that the bias adds to the utility as
`directness`. `challengeTests.cpp` checks that nobody challenges a keeper
holding the ball, and `restartTests.cpp` where a goal kick is taken from.

## What this is not

He does not drop the ball to play it with his feet, roll it with a chosen
pace, throw it overarm through the air or drop-kick it, and a punt leaves the
ground, not his hands. There is no back-pass rule — he takes a teammate's pass
in his hands — no six-second rule, no rule that the taker of a goal kick may
not play the ball twice, and the opponents need not leave the penalty area for
a goal kick. Outfield players do not kick long; lofted passes and crosses are
theirs to come (#94). Who he aims a long ball at is a teammate's position, not
a space to run onto, and the flight time is the same for every distance.
