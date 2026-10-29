# On the ball

What the player on the ball does, each time decided apart from how well he
executes it: the passes he sees, the choice among them, the pass itself, the
shot he weighs and strikes, the cross he delivers into the box, and the
touches with which he carries the ball or dribbles.

- [Pass candidates](#pass-candidates)
- [Pass decisions](#pass-decisions)
- [Passing](#passing)
- [Shot decisions](#shot-decisions)
- [Shooting](#shooting)
- [Crossing](#crossing)
- [Ball carrying](#ball-carrying)

## Pass candidates

Before a player on the ball decides, he lists his passing options and scores
each — from what he believes, not the true state. Candidate generation is a pure
function in `sim-match` (`passCandidates.hpp`),
`generatePassCandidates(state, carrierIndex, time, rules)`: the *generate
candidates* and *estimate utility* steps of the decision pipeline
(implementation plan, section 6.6). Choosing and playing the pass are separate
steps.

### Only what the carrier remembers

Every input about positions comes from the carrier's
[perception](players.md#perception) memory:

- **Receivers.** Every teammate the carrier remembers with at least
  `minConfidence` (0.3) is a candidate. The target is where the carrier believes
  the teammate is now: `estimatePosition()` of the observation.
- **Opponents.** Only opponents he remembers with at least `minConfidence` count,
  at their estimated positions and last seen velocities. An opponent he has not
  seen does not exist for the pass, however close he really is.
- **Movement limits.** The carrier does not know anyone's attributes, so he
  imagines every player with the default limits.

Who is a teammate is known from the squad; where he is, is not.

### Scores

For a candidate at `distance` meters, the pass speed is
`planPassSpeed(distance)` (see [passing](#passing)) and the lane is the
straight line from the ball to the target.

| Component            | Range   | How                                                                 |
|----------------------|---------|---------------------------------------------------------------------|
| `interceptionRisk`   | 0 … 1   | chance that some remembered opponent reaches the lane first         |
| `judgedInterceptionRisk` | 0 … 1 | the risk as he judges it: `interceptionRisk`, unless a poor decider [misjudges](#decision-making) it |
| `completion`         | 0 … 1   | `(1 − judgedInterceptionRisk) · receiverConfidence`                 |
| `progression`        | −1 … 1  | meters gained toward the opponent's goal line over the pitch length |
| `receiverPressure`   | 0 … 1   | `1 − d / pressureRadius` for the nearest remembered opponent at `d` from the target, at least 0 |
| `utility`            |         | `completion·w_c + progression·w_p − pressure·w_r − risk·w_i`, `risk` the judged one, plus `loftedBias` for a lofted pass |

**Interception risk.** The ball's time to reach a point `s` meters along the lane
follows from rolling friction, `s = v·t − a·t²/2`. Every half meter along the lane
the carrier compares it with how soon a player can come within the control radius
of that point (`estimateArrivalSeconds()` minus the radius covered at full speed):

1. The lane ends where the receiver can take the ball, the first point he reaches
   no later than the ball; beyond it, the ball is his.
2. An opponent's margin is the least time he has to spare at any point before
   that, negative if he gets there first.
3. His risk falls smoothly from 1 at a margin of −`interceptionMarginSeconds`
   (0.6 s) through ½ at 0 to 0 at +0.6 s — a cubic smoothstep, weighted by how
   sure the carrier is of him.
4. The pass survives every opponent: `interceptionRisk = 1 − Π (1 − risk)`.

**Contributions.** `passContributions(candidate, scoring)` splits the utility
into weighted parts: `completion` and `progression` add, `pressure` and `risk`
subtract, and `directness` is the scoring's `loftedBias` for a lofted pass (0
for a ground one). Their sum is the utility, bit for bit; the generator computes
it this way. `dominantContribution()` names the part with the largest absolute
value — the reason a decision trace gives.

The scores use only arithmetic and square roots, so they are identical on every
platform. Progression follows `attackingDirection()`
([match state](match-engine.md#match-state)).

### Valid candidates

A candidate can be played unless it is

| Rejection     | Reason                                                           |
|---------------|------------------------------------------------------------------|
| `kTooClose`   | closer than `minPassDistance` (2 m)                              |
| `kTooFar`     | farther than `maxPassDistance` (35 m)                            |
| `kOutOfReach` | even the planned speed stops before the target                   |
| `kUnlikely`   | `completion` below `minCompletion` (0.35)                        |
| `kOffside`    | with [offside](match-engine.md#offside) on: the receiver where the carrier believes him stands in an offside position against `offsideLine()` of the opponents the carrier remembers and the ball |

Invalid candidates stay in the list with their reason, so a decision can be
explained. Without a valid candidate the carrier has no pass to play.

### Order

Valid candidates come first, by descending utility, then the invalid ones;
equal utilities go by receiver id, a ground pass before a lofted one to the same
receiver (`orderPassCandidates()`). A goalkeeper's long balls
([goalkeeper distribution](goalkeeper.md#long-balls)) are lofted candidates
scored the same way (`scorePassCandidate()`), with the risk of losing the ball
where it comes down as interception risk, in the same list. The same state
always gives the same list.

### Configuration

`PassScoringConfig`, part of the decision configuration (see
[pass decisions](#pass-decisions)):

| Field                       | Default | Meaning                                 |
|-----------------------------|---------|-----------------------------------------|
| `minConfidence`             | 0.3     | observations below are ignored          |
| `minPassDistance`           | 2 m     | shorter passes are not considered       |
| `maxPassDistance`           | 35 m    | longer passes are not considered        |
| `interceptionMarginSeconds` | 0.6 s   | width of the interception-risk step     |
| `pressureRadius`            | 6 m     | reach of an opponent's pressure         |
| `minCompletion`             | 0.35    | less likely passes are not offered      |
| `completionWeight`          | 1.0     | `w_c`                                   |
| `progressionWeight`         | 0.8     | `w_p`                                   |
| `pressureWeight`            | 0.3     | `w_r`                                   |
| `riskWeight`                | 0.3     | `w_i`                                   |
| `loftedBias`                | 0       | added to a lofted pass's utility; the decision sets it from a keeper's directness |

### What this is not

- Passes go to where the receiver is believed to be, not into his run or into
  space.
- No tactical fit, no personality, no cognitive error in the estimates yet.

## Pass decisions

A player on the ball decides, several times a second, whether and where to pass
or to shoot, to cross, or to hold, turn, carry or dribble. The decision reads only his own
[perception](players.md#perception), picks among his
[pass candidates](#pass-candidates), his best [shot](#shot-decisions), his best
[cross](#crossing) and the best of each kind of his [carries](#ball-carrying) with a seeded softmax,
and leaves the kick or the touch to execution ([passing](#passing),
[ball carrying](#the-touch-the-ball-leaves-his-feet)). It lives
in `sim-match` (`passDecision.hpp`) and runs as the system
`makePassDecisionSystem()`.

### The pipeline

```text
perception → candidates and utilities → softmax choice → PassIntent  → execution
 (memory)     generatePassCandidates()     chooseOnBall()   ShotIntent    ball system
              generateShotCandidates()                      CarryIntent
              generateCarryCandidates()                     pendingAction
              generateCrossCandidates()
```

Every `intervalTicks` ticks (6: five times a second at 30 Hz), and in the tick
after a carrier got his touched ball back
([ball carrying](#when-he-decides)):

1. **Is there a decision to make?** Not while the ball is free, rolls ahead of
   its owner after a touch, or its owner already has an action pending.
2. **Hold.** A player keeps a ball he took less than `minHoldSeconds` (0.5 s) ago,
   measured from his last touch — a goalkeeper one he holds in his hands less
   than the distribution's `holdSeconds` (2 s). A player who owns the ball from
   the start of a state may pass at once, and so may one whose last touch was
   his own touch on in his active run.
3. **Options.** `generatePassCandidates()`, `generateShotCandidates()`,
   `generateCarryCandidates()` and, wide in the final third with the ball at
   his feet, `generateCrossCandidates()` list and score his options from his
   memory. A
   goalkeeper adds his long balls, and from his hands throws no farther than
   `throwRange` ([goalkeeper distribution](goalkeeper.md#distribution)).
4. **Choice.** A poor decider first misjudges his passes and shots
   (`misjudgeOptions()`, from the `kCognition` stream,
   [decision-making](#decision-making)). `chooseOnBall()` picks one of the
   valid passes, the best valid shot, the best valid cross or the best valid
   carry of each kind. Without a valid option -- only
   with the ball in a goalkeeper's hands, since holding the ball at the feet is
   always valid -- he keeps the ball; a goalkeeper who has held it in his hands
   for `clearSeconds` (6 s) clears it
   ([goalkeeper distribution](goalkeeper.md#the-ball-in-his-hands)).
   A carrier who holds the ball, or carries it without gaining anything,
   [looks for an option](#without-a-valid-option).
5. **Intent.** The choice becomes his pending action — for a pass the target,
   planned speed, receiver and whether it is lofted, for a shot the aimed
   point, height and speed, for a turn, carry or dribble the touch. The ball
   system plays it in the next step.

### Without a valid option

A carrier without a valid option does not stand on the ball for good: he looks
around for one and, after long enough, plays the best he has. Only a goalkeeper
with the ball in his hands has his own rule.

The same holds for a carrier who holds the ball despite options, or carries it
without gaining anything — less than `minCarryDistance` toward goal, no better
shot, nobody bypassed ([ball carrying](#holding-on-and-getting-stuck)): he looks
for a better option and, once stuck, plays the ball. A carry that gains
something goes on, and his look around keeps its time through it.

**He looks over his shoulder.** At each decision without an option he looks at
the next of his look targets (`lookTargets()`, `carrierScan.hpp`) until the next
decision, 0.2 s and two [perception](players.md#perception) updates later. He
turns his head, not his body: perception uses his scan's `gaze` instead of his
facing while he has the ball, and body and ball stay put. The scan
(`CarrierScan` in his tactical state) belongs to his possession: it ends when he
plays the ball, and counts for nothing once he has lost it or touched it other
than to carry it on.

**Where he looks.** The look targets come in two sides, taken from his
facing:

1. The **first side** is the one on which his turn toward the opponent's goal
   is shorter: a winger on the left at the opponent's goal line who faces it
   turns right, toward the box, one on the right turns left. A carrier who
   faces the goal or straight away from it turns toward the pitch's middle.
   On that side a forward -- his slot holds `runInBehind` -- looks at the
   opponent's penalty spot, everyone else at the centroid of the movement
   targets of his teammates there: where their tactic wants them, or where
   they stand without a target.
2. The **other side**: the centroid of his teammates' targets there.

A side without a teammate gives no target, and he keeps alternating between
the two while he has no option.

**He acts when stuck.** `stuckSeconds` (3 s) after the decision that first
found no option -- time for teammates to get free -- he plays the pass with
the highest utility of all he rejected. Without any pass at all he kicks the
ball long: lofted, to `clearanceTarget()`, `clearTouchlineMargin` (5 m) inside
a touchline, as far up the pitch as his longest kick reaches but no closer
than `clearGoalLineMargin` (10 m) to the opponent's goal line, on the side
where his team's [pitch control](team-tactics.md#pitch-control) is higher -- the nearer
side on a tie or without a grid. A side whose point lies closer to the ball
than the distribution's `minLongDistance` (25 m) gives way to the other, as
for a carrier at the near touchline as far up as the goal-line margin lets
him; if both do, the farther wins. The kick goes at the speed along the ground
that brings it down there after the distribution's `flightSeconds`; his
longest is the one at the passing's `maxLoftedSpeed`. Neither choice draws a
number.

Should he stand on the ball anyway, an opponent nearby
[goes at him](team-tactics.md#a-carrier-who-stands-on-the-ball).

### Seeded softmax

Among the valid options, `chooseOnBall()` picks option `i` with probability

```text
p_i = exp((u_i − u_max) / T) / Σ exp((u_j − u_max) / T)
```

for utilities `u` and temperature `T` (0.15). The best option is the most
likely, not certain: two options 0.1 apart are chosen about 2 : 1. A lower
temperature is greedier, a higher one more varied.

The draw comes from the `kAi` random stream: one number per choice, none when
there is nothing to choose. The exponential is `stableExp()`, so the same seed
makes the same choices on every platform. `T` is the player's own
[temperature](#decision-making).

### Decision-making

`PlayerAttributes::decisionMaking`, in [0, 1] and 0.5 by default
(`kDefaultDecisionMaking`), is how well a player chooses on the ball: how
reliably he picks his best option, and how well he estimates his options in the
first place (implementation plan §6.6). The default decides exactly as before
the skill existed.

**How reliably he picks.** `choiceTemperature()` scales the softmax temperature
by `1 + skillTemperatureSpread · (d₀ − d) / d₀`, for his decisionMaking `d` and
the default `d₀`:

| decisionMaking | Temperature at the defaults | Two options 0.1 apart chosen |
|----------------|-----------------------------|------------------------------|
| 1              | 0.0375                      | about 14 : 1                 |
| 0.5            | 0.15                        | about 2 : 1                  |
| 0              | 0.2625                      | about 3 : 2                  |

**How well he estimates.** A player below the default misjudges every estimate
his choice rests on: the interception risk of each pass valid or rejected only
as `unlikely`, and the block, save and goal chances of his shots.
`misjudgeOptions()` adds a triangular error of half-width
`maxMisjudgement · (1 − d / d₀)` (`misjudgementWidth()`), clamps the result to
[0, 1] and scores the candidate again with it: completion, second ball, utility
and whether it is still valid follow the misjudged values
(`judgePassCandidate()`, `judgeShotCandidate()`). At or above the default the
width is 0: he estimates as well as his perception lets him. A poor decider
may so take a pass he would have seen as too risky, or pass up a shot he
misjudged as hopeless.

Each pass gets its own error. The shots get one error each for block, save and
goal, the same for every zone of the goal: he misjudges the situation, not each
corner apart. An error per zone would make the best of 35 zones look better
than it is, and a poor decider would shoot more just for being poor.

The errors draw from their own random stream, `kCognition`: one triangular draw
per such pass, in list order, then three for the shots if his position allows
one, and nothing at all for a player at or above the default. The choice still
draws exactly one number from `kAi`.

**Both estimates stay.** A candidate keeps the estimate from his memory
(`interceptionRisk`, `blockRisk`, `saveRisk`, `goalChance`) beside the one he
judged (`judgedInterceptionRisk`, `judgedBlockRisk`, `judgedSaveRisk`,
`judgedGoalChance`), so the [decision trace](tooling.md#decision-trace) prints
both when they differ and blames an interception on his `judgement` when he
misjudged a risky pass as safe.

| Field                    | Default | Meaning                                                  |
|--------------------------|---------|----------------------------------------------------------|
| `skillTemperatureSpread` | 0.75    | how far the temperature moves from the default's, in [0, 1) |
| `maxMisjudgement`        | 0.3     | the worst decider's misjudgement half-width, in [0, 1]   |

Both are part of `DecisionConfig` and so of every replay. In the `clear-chance`
[scenario](scenarios.md#the-shot-scenarios) a decider of 1 shoots in at least
98 of 100 seeds, the default in at least 90 and a decider of 0 still in more
than half.

### Timing

A decision in the step of tick `t` becomes the pending action; the ball system
executes it in the step of `t + 1`. Several rules keep a pass from being played
twice:

- no decision while his action is pending,
- only the owner can decide, and he no longer owns the ball once it is kicked,
- the ball system clears every pending pass and shot whether it plays it or not,
- the passer cannot take the ball back for the reclaim delay (see
  [reception](ball.md#reception-interceptions-and-loose-balls)).

### Configuration

`DecisionConfig` is part of `MatchConfig` and of every replay, and holds the
`PassScoringConfig` of [pass candidates](#pass-candidates) and the
`ShotScoringConfig` of [shot decisions](#shot-decisions):

| Field            | Default | Meaning                                         |
|------------------|---------|-------------------------------------------------|
| `intervalTicks`  | 6       | ticks between decisions                         |
| `minHoldSeconds` | 0.5 s   | a new owner keeps the ball at least this long   |
| `temperature`    | 0.15    | softmax temperature over utility for an average decider |
| `skillTemperatureSpread`, `maxMisjudgement` | 0.75, 0.3 | how [decision-making](#decision-making) shifts the temperature and how far a poor decider misjudges |
| `scoring`        |         | the pass candidates' scoring configuration      |
| `shooting`       |         | the shot candidates' scoring configuration      |
| `distribution`   |         | how a goalkeeper distributes the ball ([goalkeeper distribution](goalkeeper.md#distribution)) |
| `stuckSeconds`   | 3 s     | a carrier without a valid option looks for one this long |
| `clearTouchlineMargin` | 5 m | his long kick without any pass lands this far inside a touchline |
| `clearGoalLineMargin`  | 10 m | and no closer than this to the opponent's goal line |
| `carrying`       |         | how he weighs holding, turning, carrying and dribbling ([ball carrying](#configuration-5)) |

The system rejects an interval below one tick, a negative or non-finite hold,
stuck time or margin, a temperature that is not positive and finite, a
temperature spread outside [0, 1) or a misjudgement outside [0, 1], and invalid
scoring, carrying or distribution. It runs every tick, to follow a
touched ball and to decide when a carrier gets it back, but decides at the
interval otherwise.

### Passing risk

A team with a tactic scales the scoring by the `passingRisk` `r` of its current
phase instruction ([tactics](tactics.md#tactic-model)) before the carrier decides;
`scoringForRisk()` does the arithmetic:

| Field               | Scaled by               | Effect of a bold tactic (`r` near 1)  |
|---------------------|-------------------------|---------------------------------------|
| `progressionWeight` | `0.5 + r`               | values ground gained more             |
| `riskWeight`        | `1.5 - r`               | fears interception less               |
| `minCompletion`     | `1.3 - 0.6 r`, at most 1 | offers less likely passes too        |

`r = 0.5`, the reference tactic's value, leaves the scoring unchanged, so a
team without a tactic decides like the reference tactic. A goalkeeper's scoring
also carries his tactic's directness as the `loftedBias` of his long balls
([goalkeeper distribution](goalkeeper.md#the-tactics-dial)). The factors are
simple and linear on purpose: they separate the styles without a second tuning
surface.

### What this is not

- He kicks long only as a goalkeeper or when he has nothing else.
- Where he looks ignores the opponents around him; shielding the ball is his
  move in a [duel](duels.md#duels-on-the-ball).
- Off-ball players decide elsewhere: [off-ball movement](team-tactics.md#off-ball-movement)
  and [reception](ball.md#reception-interceptions-and-loose-balls).

## Passing

A pass is split into what the player decides and how well he executes it
(implementation plan, section 6.6). The decision is a `PassIntent`; execution
turns it into a ball velocity with a random error. The rules are in `sim-match`
(`passing.hpp`); the [ball movement](ball.md#ball-movement) system executes
passes.

### The intent

`PassIntent` is the *what*: the passer, the target position, the speed the ball
should leave the foot with, and optionally the intended receiver. It sits in
the passer's `MatchState::pendingAction()` until the ball system plays it;
every state created from a spec starts without one.

Two things create an intent:

- the [pass decision](#pass-decisions) of the player on the ball, and
- a `PassCommand{playerId, target, speed, receiver}`, so a scenario or test can
  request one specific pass. It is validated when scheduled: passer and receiver
  must exist, the target must be finite and the speed positive and finite.

`planPassSpeed(distance, physics, config)` is the speed a pass over a distance
needs to reach its target still rolling at `arrivalSpeed` (4 m/s):
`sqrt(arrivalSpeed² + 2 · rollingDeceleration · distance)`, capped at `maxSpeed`
(22 m/s). `passReach(speed, physics)` is how far a pass at that speed rolls before
it stops, `speed² / (2 · rollingDeceleration)`. A pass that even `maxSpeed` cannot
carry to its target stops short.

### Execution

In the next step it runs, the ball system plays the pending pass:

1. **Only the owner passes.** If the passer does not own the ball — someone else
   has it, or it is free — or it still rolls ahead of him after a touch
   ([ball carrying](#ball-carrying)), the intent is discarded and nothing
   happens.
2. **Release.** The ball becomes free, and the passer becomes its `lastTouch`
   with the current tick.
3. **Kick.** `executePass()` gives the ball its velocity: from where the ball is
   toward the target, at the intended speed, off by a random execution error:
   - direction: up to `directionError` (0.03) meters off line per meter along
     it, uniformly distributed — about ±1.7°;
   - speed: up to `speedError` (5 %) harder or softer, uniformly distributed;
   - both grown by pressure: with opponents within `pressureRadius` (3 m) of the
     passer, the errors are multiplied by `1 + pressureErrorFactor × pressure`.
     Pressure is one minus the product of min(1, distance / `pressureRadius`)
     over the opponents — 1 − distance / `pressureRadius` for one opponent, more
     for several (`passPressure()`, from true positions: pressure is physical).
     At full pressure the default factor of 1 doubles the errors;
   - never faster than `maxSpeed`.

   A target on the ball itself is played along the passer's facing.
4. **Roll.** From then on the ball is an ordinary free ball: ground friction and
   the pitch boundary decide its trajectory and reach.

Either way the pending pass is cleared, so an intent is played at most once.

A `PassCommand` for tick `t` is applied at the start of that step and executed
in the same step. An intent the decision system writes during tick `t` is
executed in the step of tick `t + 1`.

The execution error draws exactly two numbers from the `kExecution` stream per
executed pass, so the same seed reproduces every pass. A test can set both
errors to zero to predict a pass exactly.

### Configuration

`PassConfig` is part of `MatchConfig` and of every replay:

| Field            | Default  | Meaning                                              |
|------------------|----------|------------------------------------------------------|
| `arrivalSpeed`   | 4 m/s    | the speed a planned pass keeps at its target         |
| `maxSpeed`       | 22 m/s   | the hardest ground pass                              |
| `directionError` | 0.03     | largest sideways deviation per meter along the line  |
| `speedError`     | 0.05     | largest relative deviation of the speed              |
| `pressureRadius` | 3 m      | an opponent this close puts the passer under pressure |
| `pressureErrorFactor` | 1.0 | how much full pressure grows both errors             |
| `maxLoftedSpeed` | 28 m/s   | the hardest lofted kick, along the ground            |
| `loftedErrorFactor` | 2     | how many times as far a lofted kick strays           |

The ball system rejects non-positive speeds and pressure radius, negative
errors and factors, a speed error that could reach 1 under full pressure
(`speedError × (1 + pressureErrorFactor)`, and that times `loftedErrorFactor`),
and any non-finite value, also a ground or lofted kick's direction error under
full pressure (`directionError × max(1, loftedErrorFactor) × (1 + pressureErrorFactor)`).

### Lofted passes

A `PassIntent` with `lofted` set goes through the air: its `speed` is the
ball's speed along the ground, and the ball goes up at the vertical speed that
brings it down on the target. It is executed with `loftedPassConfig()` —
`maxLoftedSpeed` for `maxSpeed`, both errors `loftedErrorFactor` times as
large — and the pace error scales its rise as it scales its speed. So far only
a goalkeeper plays one ([goalkeeper distribution](goalkeeper.md#distribution)).

### What this is not

- Every pass but a goalkeeper's long ball is a ground pass, without vertical
  velocity or spin: outfield players have no chip or lofted pass, and nobody
  spins one.
- The error depends on pressure, not yet on the passer's technique or body
  shape; those come with the player model.
- Who gets the ball is decided by
  [reception](ball.md#reception-interceptions-and-loose-balls).

## Shot decisions

A player on the ball weighs shooting against his other options. He splits the
goal he attacks into zones, judges for each what his shot would become —
blocked, saved, a goal, a second ball — and offers his best shot to the
[choice](#pass-decisions) his passes compete in. The decision records where he
aims; how well he strikes it is [execution](#shooting). Candidate generation is
a pure function in `sim-match` (`shotCandidates.hpp`),
`generateShotCandidates(state, shooterIndex, time, rules)`; the choice and the
pending shot belong to the [pass decision](#pass-decisions) system.

```text
perception → shot candidates      → one softmax with the passes → ShotIntent  → strike
 (memory)     generateShotCandidates()  chooseOnBall()             pendingAction  ball system
```

### Only what he sees

Like the [pass candidates](#pass-candidates), shot candidates read only the
shooter's [perception](players.md#perception) memory:

- **Opponents** count only if he remembers them with at least `minConfidence`
  (0.3), at their estimated positions and last seen velocities, imagined with
  the default movement limits. An opponent he has not seen neither blocks nor
  saves.
- **The keeper** is the opponent whose slot in his side's tactic guards the goal
  (`isGoalkeeper()`): who keeps goal is known from the squad, where he stands
  is not. A side without a tactic has no keeper, and a keeper the shooter does
  not remember saves nothing.
- **His own accuracy** he knows: `PlayerAttributes::shotAccuracy`, from 0
  (wild) to 1 (pinpoint), 0.5 by default ([match state](match-engine.md#match-state)).

### Where he can shoot from

Three things rule out every shot at once, before anything is scored:

| Rejection    | When                                                                   |
|--------------|------------------------------------------------------------------------|
| `kTooFar`    | the ball is farther than `maxShotDistance` (25 m) from the goal's center; a [chip](#shot-types) alone is too far beyond what it can reach no steeper than `maxLaunchSlope` |
| `kGoalUnseen`| the goal's center is outside his field of view: he looks away -- along his facing, or his look over his shoulder ([pass decisions](#without-a-valid-option)) |
| `kTooNarrow` | the goal looks narrower than `minOpening` (0.1)                        |

A player facing away would have forgotten the keeper and judge every shot an
open goal; he first turns ([ball carrying](#ball-carrying)). The **opening** is
how wide the goal looks: the distance between the unit vectors to its two posts,
`2·sin(angle / 2)` (`goalOpening()`) — about the angle in radians for a narrow
goal, 2 on the goal line between the posts — computed without trigonometry, so
the same on every platform.

### The aiming grid

The goal's mouth, between the posts and up to the crossbar, is split into
`zoneColumns` × `zoneRows` (7 × 5) zones. Each zone is a candidate, aimed at a
point on the goal line and a height:

- an inner zone at its center,
- a zone on the frame — the outer columns and the top row — at the point as
  close to the post or crossbar as his spread lets the shot stay inside, but
  never closer than `frameMargin` (0.1 m) and never beyond the zone's center.

So a pinpoint finisher aims 0.1 m inside the post, an average one keeps his
spread inside the frame, and a wild one aims at the zone's center.

### The spread

A shot ends up somewhere around its aim: a triangular spread, independent across
and up, of half-width

```text
spread = (spreadAtZero + spreadPerMeter · distance)
       · (1 + pressureSpread · pressure)
       · (0.1 + 1.8 · (1 − shotAccuracy))
```

with `distance` to the goal's center and `pressure = max(0, 1 − d / pressureRadius)`
for the nearest remembered opponent at `d`. An average finisher (0.5) spreads
0.2 m plus 4 cm per meter, a pinpoint one a tenth of that, a wild one almost
twice. The triangle's distribution function is quadratic, so each zone's share
is exact arithmetic. A low shot skids along the grass: the lowest row takes
everything below its top.

### What becomes of a shot at a zone

For each zone's center and each [shot type](#shot-types), the shot is a line from
the ball -- straight, or for a curled shot the arc that reaches the zone --
rising from the ground to the zone's height at the goal line, struck at its
type's speed and slowing like a rolling ball; a chip flies its real flight
instead. As for passes, every half meter along it the shooter compares when
the ball gets there with when a player can ([pass candidates](#pass-candidates)):

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

### A candidate's chances

Weighting every zone's outcome by the share of the spread that ends up there
gives the candidate's chances:

| Component          | How                                                                    |
|--------------------|------------------------------------------------------------------------|
| `onTarget`         | the share of the spread inside the frame                               |
| `blockRisk`        | block over the spread — a wide shot passes the zone on the frame nearest it, so a blocker in front of the goal blocks it too |
| `saveRisk`         | save over the spread inside the frame                                  |
| `goalChance`       | goal over the spread inside the frame                                  |
| `judgedBlockRisk`, `judgedSaveRisk`, `judgedGoalChance` | the three as he judges them: themselves, unless a poor decider [misjudges](#decision-making) them |
| `secondBallChance` | `reboundShare` (0.3) of the judged `blockRisk + saveRisk`: a rebound or a corner |
| `utility`          | `goal·w_g + secondBall·w_s − loss·w_k`, from the judged goal chance     |

where `loss = 1 − goalChance − secondBallChance` is the chance his team loses the
ball, again from the judged goal chance. `shotContributions(candidate, scoring)` splits the utility into `goal`,
`secondBall` and `possession`, whose sum is the utility bit for bit;
`dominantShotContribution()` names the largest, the reason a
[decision trace](tooling.md#decision-trace) gives.

**Keeping the ball** is worth 0, so a shot is only offered at a positive
utility. `w_k` (1.0) matches what a completed pass is worth
(`completionWeight`): losing the ball costs as much as keeping it is worth.
Holding the ball is also an explicit option ([ball carrying](#ball-carrying)).

### Valid candidates

Besides the three rejections of the shooter's position, a candidate is rejected
when

| Rejection     | When                                                     |
|---------------|----------------------------------------------------------|
| `kBlocked`    | the judged `blockRisk` is at least `maxBlockRisk` (0.7)  |
| `kUnlikely`   | the judged `goalChance` is below `minGoalChance` (0.05)  |
| `kNotWorthIt` | the utility is 0 or less: keeping the ball is worth more |

Every zone stays in the list with its reason, so a decision can be explained.
Valid candidates come first, by descending utility, then the rest; ties by
column, then row.

### Shot types

He chooses how to strike the ball as well as where (GDD §8.8): every zone is a
candidate once per type, `ShotType` (`shotType.hpp`), and the best of all of
them competes in the [choice](#the-choice).

| Type      | Speed                  | Path he imagines | Spin it is struck with |
|-----------|------------------------|------------------|------------------------|
| `driven`  | `shotSpeed` (25 m/s)   | a straight line rising to the zone | topspin |
| `curled`  | `curledSpeed` (20 m/s) | the arc to the zone of his curvature, bending left or right -- two candidates per zone | sidespin |
| `chipped` | `chippedSpeed` (12 m/s) | the real flight to the zone at that speed along the ground: high, slow, against the drag | backspin |

**Curled.** His curvature is `magnusCurvature · curlSpin · shotTechnique`
([curve](ball.md#curve)): `curlSpin` (60 rad/s) at perfect technique, none at 0,
when he has no curled candidates. The arc to a zone leaves off the straight
line by half the angle it turns (`aimAlongArc()`), so it passes a wall or a
defender in the way and still ends at the zone.

**Chipped.** Slow along the ground, the ball has to rise high to get there: from
16 m at 12 m/s it is 3.8 m up as it passes a keeper eight meters off his line.
He knows the flight's time against the drag (`flightSeconds()`), so a keeper
who gets back in time saves it. A chip that would have to leave steeper than
`maxLaunchSlope` (1, as the [strike's](#the-strike)) cannot get there and is
`too far`: at 12 m/s that is beyond about 18 m.

**What his technique adds to the spread.** A curl and a chip stray further than
a driven shot, by the error his technique gives the spin or the pace, widened by
pressure as the [spread](#the-spread) is and scaled by
`skillErrorFactor(shotTechnique)`:

| Type      | Extra half-width of the spread, average technique |
|-----------|-----------------------------------------------------|
| `curled`  | `magnusCurvature · spinError · L² / 2`: the curve his spin's error (`spinError`, 15 rad/s) bends it off by at the goal, `L` from the ball to the goal's centre |
| `chipped` | `2 · speedError · L · g·L / (2v²)`: the distance a pace error (`speedError`, 5 %) costs -- it grows with the square of the pace --, times the slope it comes down at |

So an average player curls rarely: from 20 m his curl strays 2.4 m further. A
player of perfect technique strays a tenth of that.

Candidates are ordered as before; ties go by column, row, then type: driven,
curled left, curled right, chipped.

### The choice

`chooseOnBall()` offers the valid passes and the **best** valid shot — one shot,
not every zone or type, or a player with seven good zones would shoot seven
times as often — to the seeded softmax of [pass decisions](#pass-decisions),
with the same [temperature](#decision-making) and exactly one draw from the
`kAi` stream per decision. A side with a tactic scales the shot scoring by the `passingRisk` `r`
of its current phase, as it does the pass scoring (`shotScoringForRisk()`):

| Field           | Scaled by               | Effect of a bold tactic (`r` near 1) |
|-----------------|-------------------------|--------------------------------------|
| `goalWeight`    | `0.5 + r`               | values a goal more                   |
| `lossWeight`    | `1.5 - r`               | fears losing the ball less           |
| `minGoalChance` | `1.3 - 0.6 r`, at most 1 | offers less likely shots too        |

At `r = 0.5` nothing changes.

### The shot

A chosen shot becomes the shooter's pending action, a `ShotIntent` with the aimed
point, height and speed, its type and a curled shot's curvature
([match state](match-engine.md#match-state)). The ball system strikes
it in the next step, if he still has the ball, records `ShotAttempted` — shooter,
where from, the aimed point and height, the speed, the distance, the opening and
the type — and the state's `lastShot()`. A ball won back from a shot is loose, not a pass
received or intercepted ([match events](match-engine.md#match-events-and-decision-diagnostics)).

How the ball leaves his foot and what becomes of it — a goal, a save, a block,
the woodwork, a miss — is [shooting](#shooting): the strike flies at the aimed
point and height at its type's speed, off by his execution error.

### Scenarios and guardrails

The `clear-chance`, `hopeless-angle` and `blocked-lane`
[scenarios](scenarios.md#the-shot-scenarios) check the decision over 100 seeds:
an eight-meter chance with only the keeper to beat is taken at least 90 times;
from the goal line six meters wide of the post the goal is `too narrow` every
time and the free teammate gets the ball; with a defender two meters in front the
player passes more often than he shoots. The reduced P2
[style benchmark](tooling.md#p2-acceptance) requires every side to shoot 0.5 to
40 times per six-minute match.

At the time of writing, an average finisher facing a keeper on his line judges
his best shot from straight in front of the goal at a goal chance of about 0.67
from 8 m, 0.41 from 12 m, 0.09 from 18 m and 0.06 from 22 m.

### Configuration

`ShotScoringConfig` is the `shooting` part of the decision configuration
(`DecisionConfig`, part of `MatchConfig` and of every replay):

| Field                | Default | Meaning                                                  |
|----------------------|---------|----------------------------------------------------------|
| `minConfidence`      | 0.3     | observations below are ignored                           |
| `maxShotDistance`    | 25 m    | farther from the goal's center, no shot                  |
| `minOpening`         | 0.1     | a narrower goal offers no shot                           |
| `zoneColumns`        | 7       | the aiming grid across the goal, at least 2              |
| `zoneRows`           | 5       | the aiming grid up the goal, at least 1                  |
| `shotSpeed`          | 25 m/s  | how fast a driven shot leaves the foot                   |
| `curledSpeed`, `chippedSpeed` | 20 m/s, 12 m/s | how fast a curled and a chipped shot leave it ([shot types](#shot-types)) |
| `curlSpin`           | 60 rad/s | the sidespin of a curl at perfect technique             |
| `spinError`, `speedError` | 15 rad/s, 0.05 | the strike's errors at average technique, which widen a curl's and a chip's spread |
| `maxLaunchSlope`     | 1       | the strike's steepest launch: a chip that needs more is `too far` |
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

### What this is not

- He judges a driven or curled shot as a line from the ball to a point of the
  goal, slowing like a rolling ball, the ball a point: he knows neither the
  [flight](#shooting) his strike takes nor the thickness of the posts. Only a
  chip he judges by its flight.
- A poor decider's misjudgement is noise around his estimates, not a
  personality bias toward shooting or passing. Near 0 or 1 the clamp leaves
  it one-sided: a very safe pass looks a little riskier on average.

## Shooting

A [shot decision](#shot-decisions) says where a player aims; execution says
what he makes of it: how the ball leaves his foot, what it meets — a defender,
the keeper, the frame — and what it becomes: a goal, a save, a block or a miss.
The strike is in `sim-match` (`shooting.hpp`), the frame in `goalFrame.hpp`; the
[ball system](ball.md#ball-movement) puts them together.

```text
ShotIntent   → strike        → flight → body · frame · keeper · line → ShotResolved
 pendingAction  executeShot()   ballAfter()  deflectShot()  findWoodworkHit()   GoalScored
```

### The strike

The ball system strikes a pending shot in the step after it was decided, if the
shooter still has the ball at his feet. The ball leaves the grass at his feet on the flight
that brings it to the aimed height at the aimed point of the goal line -- along
its arc for a [curled](#shot-types) shot:
`launchVerticalVelocity()` solves the closed-form flight of
[ball movement](ball.md#ball-movement) for the vertical speed that does it, at the
speed along the ground the decision chose (`ShotIntent::speed`). Such a flight
rises and falls once and does not touch the ground on the way.

- A shot aimed at the grass, or below it, is struck along it and rolls.
- The ball leaves no steeper than `maxLaunchSlope` (1, which is 45 degrees). A
  target that would need more — the top corner from two meters — gets the
  nearest the shooter can do.

That is a perfect strike. `executeShot()` adds the player's error, always six
draws from the `kExecution` stream:

| Error          | Size                                         | Skill                     |
|----------------|----------------------------------------------|---------------------------|
| across the goal | triangular, half-width `spread`             | `shotAccuracy`            |
| up and down    | triangular, half-width `spread`              | `shotAccuracy` and `shotTechnique` |
| pace           | uniform, up to `speedError` (5 %) of the speed | `shotTechnique`         |
| spin           | uniform, up to `spinError` (15 rad/s)        | `shotTechnique`           |

```text
spread = (spreadAtZero + spreadPerMeter · distance) · errorFactor
```

with `distance` from the ball to the aimed point: 0.2 m plus 4 cm per meter,
the spread the [decision](#the-spread) judges the shot by. Each
skill scales its errors by `skillErrorFactor(skill) = 0.1 + 1.8 · (1 − skill)`:
a tenth at 1, exactly as given at 0.5, almost twice at 0. The aim strays along
the goal line and in height, and the launch is solved for the point it strays
to — so **accuracy** decides where the ball goes and **technique** how cleanly
it is struck: whether it is skied or scuffed, how hard, and with what spin.
However badly struck, a shot keeps between half and one and a half times its
intended pace.

A clean strike carries the spin its [type](#shot-types) asks for, the spin error
on top:

- **driven**: topspin, `topspin` (30 rad/s) times the shooter's technique;
- **chipped**: backspin, `chipBackspin` (20 rad/s) times his technique;
- **curled**: no topspin, and the sidespin of the intent's curvature,
  `curvature / magnusCurvature`. The ball leaves along `aimAlongArc()`, off the
  straight line to the point it strays to by half the angle its path turns, and
  the launch is solved for the arc's length. A curl that strays onto the grass
  rolls straight at its point: only a flight curves ([curve](ball.md#curve)).
  Its spin error bends the whole arc, so it ends up `k · L² / 2` off for an
  error of curvature `k`, which the decision knows; however badly struck, it
  bends no sharper than the sharpest arc `aimAlongArc()` would take to its
  point, so it curls in and never loops.

Top- and backspin decide how the ball comes off the grass when it bounces,
sidespin how it [curves](ball.md#curve) in the air.

#### What widens the error

`shotConditions()` reads four things off the true state, each in [0, 1], and
`shotErrorFactor()` multiplies them into the factor above — 1 for a player in
the clear, standing or running at his shot, on his strong foot, with a settled
ball:

| Condition   | What it is                                                                 | At its worst |
|-------------|----------------------------------------------------------------------------|--------------|
| `pressure`  | the opponents close to him, as for a pass (`passPressure()`)               | × (1 + `pressureErrorFactor`) |
| `imbalance` | how much of his top speed the shooter moves at across or away from his shot | × (1 + `balanceErrorFactor`) |
| `weakFoot`  | the target lies on the side of his weaker foot                             | × (1 + `weakFootErrorFactor` · (1 − `weakFootAccuracy`)) |
| `unsettled` | he has only just received the ball, and it came fast                       | × (1 + `unsettledErrorFactor`) |

Every factor is 1 by default, so each condition doubles the errors at its
worst, and all four together make them sixteen times as wide.

- **Balance.** The part of his velocity that points along the shot costs
  nothing: running onto the ball is how a shot is struck. What he moves at
  sideways or backwards, over his `maxSpeed`, is his imbalance.
- **Foot.** A player has a `strongFoot`, right by default. A target more than
  `weakFootSide` (0.3, about 17 degrees) to the side of the other foot is struck
  with that one — for a right-footed player a target to his left, where left is
  the side of growing pitch y for a player facing growing pitch x. A
  `weakFootAccuracy` of 1 is a two-footed player, 0 doubles his errors, and the
  default 0.5 makes them half as wide again.
- **The ball.** A ball received within the last `unsettledSeconds` (1 s) is not
  settled: fully unsettled the moment it arrives at `unsettledBallSpeed`
  (20 m/s) or faster, less the slower it came and the longer he has had it. A
  ball he lost and won back since is no longer the one he received.

### On the way

A shot is **open** from the strike until its outcome is recorded;
`MatchState::lastShot()` holds it. While it is open, three things can happen to
the ball, and none of them ends the shot.

**A body in the way.** A shot at least `deflectionSpeed` (10 m/s) fast is not
taken at the feet by an outfield player, teammate or opponent. It comes off
him: within `blockRadius` (0.5 m) of its way, where the ball is no higher than
`blockReach` (1.8 m), `deflectShot()` sends it on with a share of its speed
between `minDeflectedSpeed` and `maxDeflectedSpeed` (0.1 to 0.9), turned up to
`deflectionSpread` (1 m to the side per meter along) off its line and lifted by
up to `deflectionLift` (3 m/s) — three draws from the `kExecution` stream. The
player becomes the ball's last touch, so a ball he puts behind his own goal
line is a corner. A ball that keeps less than `blockedBelow` (half) of its
speed was *blocked*; the rest of the tick it flies on from where it was hit.
An opponent a little farther off, the ball low enough, lunges at it and blocks
it or misses ([tackles and blocks](duels.md#tackles-and-blocks)).
A slower shot, and every other ball, is received as it always was
([reception](ball.md#reception-interceptions-and-loose-balls)).

**The keeper.** The keeper of the goal a shot attacks does not take it like
any ball. He reacts, dives, and where the ball passes him he holds it, parries
it or is beaten ([shot stopping](goalkeeper.md#shot-stopping)). A ball he parries flies on
like a deflected one, with him as its last touch; the shot stays open, and was
`saved` if he parried it on its way in.

**The frame.** Posts and crossbar are round bars of `radius` (0.06 m): the
posts stand on the goal line at the posts of the
[pitch geometry](match-engine.md#match-geometry), the crossbar lies on top of the goal's
height. The ball is a sphere of `kBallRadius` whose lowest point is its height.
`findWoodworkHit()` finds the first moment of a tick the two touch while the
ball moves into the bar, and the ball leaves with its speed into the bar
mirrored and scaled by `restitution` (0.5), the rest kept and its spin gone: it
comes back into play, goes out, or goes in off the post. The ball system
follows the rebound for the rest of the tick. This holds for every free ball,
not only for shots.

### Goals

A ball that leaves the pitch stops on the line where it crossed it. If that is a
goal line, between the posts and no higher than the crossbar's underside, it is
a **goal** — for shots, passes and deflections alike:

- The goal counts for the side attacking that end; the state's `score()` goes
  up by one and `lastGoal()` remembers it.
- The **scorer** is the shooter of that side's open shot, even if the ball went
  in off a defender. Without such a shot it is whoever touched the ball last —
  an **own goal** if he plays for the other side — and nobody for a ball no
  player had touched.
- The **assist** goes to the teammate whose pass the scorer last received, if
  that is how he came by the ball: `lastReception()` remembers whose pass a
  controlled ball was. A loose ball or an interception earns none, whoever the
  last pass was meant for.
- `GoalScored` records all of it with the score it makes.

With [restarts](match-engine.md#restarts) enabled, the side that conceded kicks off from
the centre spot, with both sides [lined up](match-engine.md#the-kickoff) in their
own halves.
Otherwise the ball stays on the line, as every ball that leaves the pitch does.

### Outcomes

Every shot gets exactly one `ShotResolved`, at the first of these:

| The ball                          | Outcome                                                           |
|-----------------------------------|-------------------------------------------------------------------|
| crosses a goal line in the frame  | `goal`                                                            |
| leaves the pitch anywhere else    | `offTarget`; `saved` if the keeper parried it on its way in; `blocked` if it came off an outfield player |
| comes to rest                     | the same                                                          |
| is taken by the opposing keeper   | `saved` if it was on its way into the goal or he had parried it so before, `offTarget` if on its way past; `blocked` if it had come off an outfield player |
| is taken by an opposing outfield player | `blocked`, or `saved` if the keeper had parried it on its way in |
| is taken by the shooter's own side | `offTarget`, `saved` or `blocked` as when it leaves the pitch    |

A shot **on target** is a `goal` or `saved`. Whether the keeper saved a shot is
asked of the flight he interrupted: `predictGoalLineCrossing()` follows the
ball to the goal line and the frame says whether it would have gone in.

### Events

All name the shot by its `shooter` and the tick it was struck in, `shotTick`, so
an analysis can put a chance together from its events
([match events](match-engine.md#match-events-and-decision-diagnostics)):

| Event             | Carries                                                                        |
|-------------------|--------------------------------------------------------------------------------|
| `ShotAttempted`   | where from, the aimed point and height, the point and height the ball really left for (`struckAt`, `struckHeight`), its speed, and the goal's distance and opening |
| `ShotDeflected`   | the player it came off, where and how high, and whether that blocked it         |
| `ShotHitWoodwork` | the part of the frame — `postAtMinY`, `postAtMaxY`, `crossbar` —, where and how high |
| `ShotResolved`    | the outcome: `goal`, `saved`, `offTarget` or `blocked`                          |
| `GoalScored`      | the side, the scorer, the assist, whether it was an own goal, and the score     |

A ball taken from a shot, or from a deflection of one, is a `LooseBallRecovered`,
never a pass received or intercepted.

### Configuration

`ShotConfig` is the `shooting` part of `MatchConfig`, and of every replay:

| Field                  | Default  | Meaning                                                   |
|------------------------|----------|-----------------------------------------------------------|
| `spreadAtZero`         | 0.2 m    | an average shooter's spread at the goal line              |
| `spreadPerMeter`       | 0.04     | and how much it grows per meter to the aimed point        |
| `speedError`           | 0.05     | how much harder or softer an average strike is            |
| `maxLaunchSlope`       | 1.0      | the steepest a ball leaves the foot: vertical over ground speed |
| `topspin`              | 30 rad/s | the topspin of a clean strike at perfect technique        |
| `chipBackspin`         | 20 rad/s | the backspin of a clean chip at perfect technique         |
| `spinError`            | 15 rad/s | how far an average strike's spin is off                   |
| `pressureErrorFactor`  | 1.0      | how much full pressure widens the errors                  |
| `balanceErrorFactor`   | 1.0      | how much a shot fully off balance does                    |
| `weakFootErrorFactor`  | 1.0      | how much a weak foot without any accuracy does            |
| `unsettledErrorFactor` | 1.0      | how much a fully unsettled ball does                      |
| `weakFootSide`         | 0.3      | how far to the weak side a target is struck with that foot |
| `unsettledSeconds`     | 1 s      | how long a received ball takes to settle                  |
| `unsettledBallSpeed`   | 20 m/s   | a ball received this fast is fully unsettled              |
| `deflectionSpeed`      | 10 m/s   | a shot at least this fast comes off outfield players      |
| `blockRadius`          | 0.5 m    | how far from the ball's way a body deflects it            |
| `blockReach`           | 1.8 m    | and up to which height                                    |
| `lungeRadius`, `lungeHeight` | 1 m, 0.9 m | how near and how low a shot an opponent lunges at to block it ([tackles and blocks](duels.md#tackles-and-blocks)) |
| `minDeflectedSpeed`    | 0.1      | the least share of its speed a deflected ball keeps       |
| `maxDeflectedSpeed`    | 0.9      | the most                                                  |
| `blockedBelow`         | 0.5      | below this share the deflection was a block               |
| `deflectionSpread`     | 1.0      | how far a deflection turns the ball off its line          |
| `deflectionLift`       | 3 m/s    | how much it lifts it                                      |

`WoodworkConfig` is the `woodwork` part:

| Field         | Default | Meaning                                            |
|---------------|---------|----------------------------------------------------|
| `radius`      | 0.06 m  | how thick posts and crossbar are, from their axis  |
| `restitution` | 0.5     | the share of its speed into the frame a ball keeps |

`validate()` rejects values outside their rules; see `shooting.hpp` and
`goalFrame.hpp`. The players' side of it is `PlayerAttributes`
([match state](match-engine.md#match-state)): `shotAccuracy`, `shotTechnique`, `strongFoot`
and `weakFootAccuracy`.

### Tests and guardrails

`tests/unit/sim-match/shootingTests.cpp` holds the strike — an exact shot
arrives at the aimed point and height, every error stays within its bounds and
follows its skill and its conditions —, `goalFrameTests.cpp` the rebounds off
posts and crossbar, and `shotOutcomeTests.cpp` what becomes of a shot in the
ball system: a goal, a post, the crossbar, a block, a deflected goal, a save by
a keeper standing set, a wide shot, a shot that stops short, and that every
shot has exactly one outcome.

The `clear-chance` [scenario](scenarios.md#scenario-catalogue) checks the whole of it over 100
seeds: the first shot is a goal 55 to 90 times. At the time of writing an
average finisher scores 72 of them and hits the woodwork with 48 — he aims
0.3 m inside the post with a spread of 0.5 m, and the decision that sends him
there counts every ball inside the frame as on target, while a ball within
17 cm of a post's axis hits it. From eight meters the ball is past the keeper
before he has reacted.

### What this is not

- Outfield players do not block on purpose; a body in the way is a body in
  the way. The keeper is [his own system](goalkeeper.md#shot-stopping).
- A header at goal is a shot like any other
  ([aerial duels](duels.md#aerial-duels-and-headers)), struck from the head.
- He curls or chips only as a shot; crosses come with their own action.
- A goal counts and is kicked off, but no match ends with a result yet.

## Crossing

A wide player in the final third can deliver the ball into the box as an
action of its own (GDD §8.5): not at a teammate's feet but at an area of the
box or at a run into it, in the air on the ball's flight, or cut back along the
ground. Candidate generation is a pure function in `sim-match`
(`crossCandidates.hpp`), `generateCrossCandidates(state, carrierIndex, time,
rules)`; the choice belongs to the [pass decision](#pass-decisions) system, the
delivery to the ball system.

```text
perception → cross candidates        → one softmax with the passes → PassIntent with a cross → strike
 (memory)     generateCrossCandidates()  chooseOnBall()                pendingAction            ball system
```

### Who crosses, and where to

He has crosses only when crossing is `enabled` and the ball is **wide in the
final third**: within `maxDepthShare` (0.3) of the pitch length of the goal
line he attacks and outside the width of the penalty area. Then every area of
the box is a candidate:

| Area          | Where, from the goal's centre                                  | How     |
|---------------|----------------------------------------------------------------|---------|
| `nearPost`    | the goal area's depth in, half the goal's width to his side    | in the air |
| `penaltySpot` | the penalty spot                                               | in the air |
| `farPost`     | the goal area's depth in, half the goal's width to the far side | in the air |
| `cutBack`     | the penalty spot's distance in, half the goal area's width to his side | along the ground |
| `run`         | the movement target of a teammate whose target lies in the penalty area -- one per such teammate | in the air |

A run is what his side sends a teammate to do, which a team knows; everything
else he reads from his own [perception](players.md#perception), like a pass.

Every area in the air is a candidate twice, **floated** -- leaving at
`crossSpeed` (16 m/s) along the ground, arriving at `headerHeight` (1.6 m) --
and **driven**, at `drivenSpeed` (24 m/s), arriving at `drivenHeight` (0.9 m),
below the head. Which suits the situation follows from his chances: a driven
cross beats a keeper and defenders to the ball, a floated one gives a runner
time to arrive. Either bends toward the goal -- to the side of its line the
goal's centre is on -- with `curlShare` (½) of the curl his `shotTechnique`
gives a [curled shot](#shot-types), along `aimAlongArc()`, and his technique
and conditions decide how cleanly it is struck ([the delivery](#the-choice-and-the-delivery)).
A cut-back rolls at `cutBackSpeed` (12 m/s).

### A cross's chances

For each candidate he compares when the ball gets there -- its flight against
the drag (`flightSeconds()`), or its roll -- with when his remembered players
do, as for a [long ball](goalkeeper.md#distribution):

- **The runner** is the teammate who gets there first, `runnerSeconds` -- for a
  run the teammate making it.
- **An opponent's margin** is how much later than the runner he gets there,
  each counted from no earlier than the ball. The keeper reaches a cross in the
  air in his penalty area with his hands (`handsRadius`), everyone else within
  the control radius. Along a cut-back's way he can also cut it out, as a
  pass's [interception risk](#scores): his risk is the larger of the two. It
  falls smoothly from 1 at −`contestMarginSeconds` (0.5 s) through ½ at 0 to 0
  at +0.5 s, weighted by how sure the crosser is of him; the keeper's is the
  cross's `keeperClaim`.
- **A late runner** misses it: the ball does not wait. A runner who arrives
  after it misses by the same step, half the time at `contestMarginSeconds`
  late, always at twice that.

| Component    | How                                                                   |
|--------------|-----------------------------------------------------------------------|
| `winChance`  | `Π (1 − risk)` over his remembered opponents, times the chance the runner is there in time, times how sure he is of the runner; 0 for a cross that does not get there |
| `threat`     | how wide the goal looks from the area over how wide it looks from the penalty spot, at most 1 (`goalOpening()`) |
| `utility`    | `win·w_w + win·threat·w_t − (1 − win)·w_l`                            |

`crossContributions()` splits the utility into `win`, `threat` and `loss`, whose
sum it is bit for bit; `dominantCrossContribution()` names the largest, the
reason code a [decision trace](tooling.md#decision-trace) gives. A tactic scales
the scoring by the `passingRisk` of its current phase -- in the final third its
final-third instruction -- as it does a pass's (`crossScoringForRisk()`): the
threat `0.5 + r` times, losing the ball `1.5 − r` times, the win chance a cross
needs `1.3 − 0.6 r` times.

| Rejection     | When                                                           |
|---------------|----------------------------------------------------------------|
| `noRunner`    | no teammate he remembers gets there                            |
| `outOfReach`  | the ball does not get there, or would have to leave faster than the passing's `maxLoftedSpeed` -- `maxSpeed` for a cut-back -- or steeper than the shot scoring's `maxLaunchSlope` |
| `unlikely`    | `winChance` is below `minWinChance` (0.2)                      |

Valid candidates come first by descending utility, then the rest; ties by area,
runner, then speed. A poor decider does not
[misjudge](#decision-making) his crosses; only his temperature applies.

### The choice and the delivery

`chooseOnBall()` offers the **best** valid cross -- one, not every area -- to
the seeded softmax with the passes, the best shot and the carries, last in the
list, so a decision without a cross draws as before. Chosen, it becomes a
`PassIntent` with the cross's area, its height and curvature, meant for the
runner: the ball system plays it as a pass, so the
[reception](ball.md#reception-interceptions-and-loose-balls), the
[duels](duels.md#duels-on-the-ball) and the
[aerial duels](duels.md#aerial-duels-and-headers) contest it like any other
ball, and the keeper claims it with his hands where he gets to it first.

- **In the air** it is struck like a shot (`executeShot()`): the flight to its
  height at its target along its arc, with the crosser's errors and conditions
  of [shooting](#the-strike) -- accuracy scatters it across its way,
  technique its lift, pace and spin -- in the six draws from `kExecution` a
  shot makes, none of a pass's.
- **A cut-back** is a ground pass ([passing](#passing)).

The ball system records `PassAttempted` and then `CrossAttempted` with the
area, the target, the height, the runner and the speed it left with
([match events](match-engine.md#match-events-and-decision-diagnostics)). The
diagnostics keep every cross with its area and the weights it was scored with
(`crosses`, `crossScoring`), the outcome `crossed`.

### Configuration

`CrossScoringConfig` is the `crossing` part of `DecisionConfig`, part of
`MatchConfig` and of every replay:

| Field                  | Default | Meaning                                                |
|------------------------|---------|--------------------------------------------------------|
| `enabled`              | true    | whether he crosses at all                              |
| `minConfidence`        | 0.3     | observations below are ignored                         |
| `maxDepthShare`        | 0.3     | how far from the goal line, as a share of the length   |
| `crossSpeed`, `headerHeight` | 16 m/s, 1.6 m | a floated cross                              |
| `drivenSpeed`, `drivenHeight` | 24 m/s, 0.9 m | a driven cross                              |
| `cutBackSpeed`         | 12 m/s  | a cut-back                                             |
| `curlShare`            | 0.5     | the share of his shot's curl a cross bends with        |
| `contestMarginSeconds` | 0.5 s   | width of an opponent's risk step                       |
| `minWinChance`         | 0.2     | less likely to reach his side first, not offered       |
| `winWeight`, `threatWeight`, `lossWeight` | 1, 1, 0.3 | `w_w`, `w_t`, `w_l`              |

### Scenarios

The [cross scenarios](scenarios.md#the-cross-scenarios) show an early cross
for a run into the box, a cut-back from the goal line and a cross a keeper
quicker than the crosser imagines claims.

### What this is not

- He crosses only when wide in the final third; a ball into the box from
  elsewhere is a pass.
- A cross is not blocked: the [block](duels.md#tackles-and-blocks) is for shots.
- He does not wait for a run to be timed; he decides five times a second as
  always, and a later decision sees the runner closer.

## Ball carrying

Between receiving and releasing the ball, a player can hold it, turn with it,
carry it into space or dribble past an opponent (GDD section 8.5). The
[pass decision](#pass-decisions) generates these as candidates, scores them
from his [perception](players.md#perception) only, and chooses among them, the
passes and the best shot with the same seeded softmax. The ball system executes
them: the touch, its error, the ball rolling ahead of him. Candidates and
scoring are in `sim-match` (`carryCandidate.hpp`, `carrying.hpp`).

### The touch: the ball leaves his feet

Every turn, carry or dribble starts with a **touch**: the ball leaves his feet
as a short, slow pass ahead of him and rolls freely until he reaches it again.
He keeps possession all along — `BallState::owner` stays his, so team
possession, [tactical phases](team-tactics.md#match-phases), pressing and
analytics see one possession — but the ball moves on its own.
`BallState::touch` (`CarryTouch`) records the touch's tick and how many seconds
later he may play it again.

- **Who gets to it.** The ball rolls, leaves the pitch and goes into a goal as a
  free ball would. The first player to reach it takes it, by the
  [reception](ball.md#reception-interceptions-and-loose-balls) rules, with two differences: the carrier himself
  may take it once the touch's seconds have passed, without the reclaim delay,
  which is for passes, and his teammates leave it to him.
- **He gets it back.** Reaching it, he has it at his feet again
  ([possession](ball.md#possession)), and the touch ends. His last touch stays the
  touch: getting his own ball back is no new possession.
- **An opponent gets it.** An opponent who gets to it first wins it: `BallWon`
  with the cause `looseTouch`, then `PossessionChanged`. A goalkeeper who
  reaches it in his own area takes it in his hands.
- **It leaves the pitch.** A touched ball that leaves the pitch is nobody's, and
  the [restart](match-engine.md#restarts) system settles who plays on.

While the ball is ahead of him, nobody takes him on in a
[duel](duels.md#duels-on-the-ball) — there is nothing at his feet — and a duel
he is in ends. An opponent wins it by reaching it: the earliest of those who
reach it before him [chases it](ball.md#going-after-the-ball). A touch he keeps,
nobody chases.

#### The touch's error

The touch is a pass: `executePass()` sends the ball toward the touch's target
at its planned speed, both off by a random error from the `kExecution` stream,
grown by [pressure](#passing). The error is the pass error times the touch's
**error factor**: from `maxTouchError` (3) at a `ballControl` of 0 to
`minTouchError` (0.5) at 1, linear; a dribble's times a factor from
`maxDribbleError` (1.5) at a `dribbling` of 0 to `minDribbleError` (0.5) at 1.
A heavy touch leaves the ball farther from where he meets it, so an opponent may
get there first: losing the ball is geometry, never a roll of its own. The
speed error stays at most `0.9 / (1 + pressureErrorFactor)` (`0.45` by
default), so pressure cannot stop the ball dead or turn it round.

#### Running with the ball

With the ball at his feet or rolling ahead, a player runs at most
`runningSpeed()` (`playerMovement.hpp`): `maxSpeed` times a factor from
`BallPhysics::minCarrySpeedFactor` (0.8) at no ball control to
`maxCarrySpeedFactor` (0.95) at full control, linear. Acceleration does not
change. Every system that moves him or predicts his move uses
`stepPlayerMovement()` at that speed. A goalkeeper with the ball in his hands
runs at full speed.

While the ball rolls ahead, he runs every tick to where he meets it
(`findInterception()`, `pursuit.hpp`, at his speed with the ball). If that point
lies within 45 degrees of his run, he runs on through it — as far again as his
run goes beyond it, not past the run's target — instead of braking; otherwise he
brakes to take it.

### The options

`generateCarryCandidates()` lists a carrier's options with the ball at his
feet, from his memory only: opponents he remembers with at least
`minConfidence` (0.3), where he believes they are now. A goalkeeper with the
ball in his hands has none (he only distributes it), and neither does anyone
with carrying switched off (`enabled`).

#### Hold

Holding the ball is always valid: he stays where he is, the ball at his feet.
Its utility is `holdWeight · (1 − pressure)`, where the pressure of the nearest
remembered opponent at distance `d` is `1 − d / pressureRadius` (6 m), at least
0. A player who holds the ball also [looks round](#holding-on-and-getting-stuck)
for an option.

#### Carry and turn: where to

A carry runs along a **direction**, and the directions come from the
situation, never from a fixed set:

1. **Toward the goal**: the centre of the goal he attacks.
2. **Through the gaps**: the opponents within `gapRadius` (20 m), sorted round
   him by their direction from him -- half-planes and cross products, no
   trigonometry -- leave a gap between each two neighbours. He carries through
   the middle of each: the normalized sum of the two unit vectors, or its
   opposite for a gap wider than half a turn. One opponent leaves one gap, behind
   him.
3. **Away from pressure**: straight away from the nearest opponent.
4. **Into space**: toward the cell of the [pitch-control](team-tactics.md#pitch-control) grid
   his team controls best within the reach of a carry and at least
   `minCarryDistance` away -- ties to the cell farther up the pitch.

Of directions whose unit vectors have a dot product of at least
`sameDirection` (0.98), only the first counts.

**How far.** Along a direction he carries the ball as far as `horizonSeconds`
(2 s) at his running speed take him, checked meter by meter, no farther than
`pitchMargin` (1 m) inside the lines, and short of the first point an opponent
would get to, within `controlRadius`, less than `safetySeconds` (0.3 s) after
him. A carry shorter than `minCarryDistance` (2 m) is rejected as `kTooShort`.

**A turn** is a carry in a direction away from where he faces: its dot product
with his facing below 0. It goes `turnDistance` (3 m) at most, and only needs
to leave the spot by `minTouchDistance`. It takes time: `turnSeconds` (0.5 s)
standing, plus the time it takes him to stop his run along his facing, his speed
that way over his acceleration.

**The first touch** plays the ball to where he meets it: `touchSeconds` (0.5 s)
of running at his speed with the ball ahead of him, between `minTouchDistance`
(1 m) and `maxTouchDistance` (5 m), and no farther than the carry. Its speed is
the one that rolls it there in the time he needs to get there
(`estimateArrivalSeconds()`), from `d = v·t − a·t²/2`, at most the passing's
`maxSpeed`. A ball that would stop before then -- a slow turn, `d < a·t²/2` --
is played to stop where he meets it, `v² = 2·a·d`, rather than to roll past.
He may play it again after `touchShare` (0.5) of that time. Each
later touch is decided anew when he has the ball back.

#### Dribble

A dribble is aimed at an opponent: at each remembered opponent no farther than
`dribbleRadius` (6 m) from the ball and standing in the direction he attacks
-- the unit vector toward him and the attacking direction with a dot product of
at least `dribbleCone` (0.5) -- but farther than `dribbleClearance` (2.5 m), on
either side of him: two candidates per opponent. Not for a goalkeeper.

The touch goes past the opponent: toward the point `dribbleBeyond` (1.5 m)
behind him, as far to his side as makes the ball's way pass him at
`dribbleClearance` -- for the opponent at distance `d`, `w = c·(d + b) /
sqrt(d² − c²)` to the side. A point less than `pitchMargin` inside the lines, as
for a carry, gives no dribble. His
`dribbling` shortens the time he plans to the ball, from `slowDribbleShare`
(1.2) times a carry's at 0 to `quickDribbleShare` (0.8) at 1, so a good
dribbler plays the ball past faster, and errs less.

Whether he gets past is geometry: whether the opponent reaches the ball
first. There is no feint; a dribble plays the ball past instead of fighting a
[duel](duels.md#duels-on-the-ball) for it.

### Scoring

Every option but holding is scored by the same parts, each at its target:

| Part          | Value                                                         | Weight                     |
|---------------|---------------------------------------------------------------|----------------------------|
| `keep`        | the chance that no remembered opponent gets to the touched ball first | `keepWeight` (0.8) |
| `progression` | how much nearer the centre of the goal he attacks the target lies than the ball, over the pitch length | `progressionWeight` (0.8) |
| `space`       | how much more of the target than of the ball's spot his team controls ([pitch control](team-tactics.md#pitch-control)), 0 without a grid | `spaceWeight` (0.2) |
| `shot`        | how much better a shot from the target is than one from the ball, at least 0 | `shotWeight` (0.6) |
| `bypassed`    | the remembered outfield opponents between the ball and the goal, along the attacking direction, that the target leaves behind | `bypassWeight` (0.15) each |
| `risk`        | `1 − keep`, subtracted                                         | `riskWeight` (0.3)         |
| `turn`        | a turn's seconds, subtracted                                   | `turnCost` (0.6) per second |

`keep` is the pass's interception risk turned round: along the touch's way, the
smallest time each opponent has to spare reaching the ball before it gets
there, smoothed from 1 at `−lossMarginSeconds` (0.4 s) through 1/2 at 0 to 0 at
`+lossMarginSeconds`, weighted by how sure he is of the opponent, and combined
over all of them. A candidate with `keep` below `minKeepChance` (0.35) is
rejected as `kUnlikely`.

**Progression toward the goal**, not toward its line: a carry along the goal
line into the corner gains nothing, so a player does not wander off into it.

**The shot** from a point is the goal's opening there (`goalOpening()`) over
`fullOpening` (0.6), at most 1, falling linearly with the distance to the goal's
centre to 0 at `shotDistance` (25 m). Only the improvement over the ball's spot
counts: a carry that opens the angle is worth more, one that keeps it nothing.
The shot candidates decide at the next touch.

`carryContributions()` returns the weighted parts, whose sum is the utility bit
for bit; `dominantContribution()` names the largest.

#### The situation shifts the weights

A side with a [tactic](tactics.md#tactic-model) scores its carries for the phase its team is
in with the ball, by `carryScoringForPhase()`: each weight times its phase
factor.

| Phase                | Factors                                   | Effect                               |
|----------------------|-------------------------------------------|--------------------------------------|
| build-up             | risk ×1.5                                 | no carry that could lose the ball near his own goal |
| progression          | none                                      |                                      |
| final third          | shot ×2                                   | carries that open a shot              |
| attacking transition | progression ×2, bypassed ×2, risk ×0.8    | the counter: ground and opponents left behind |

Then `carryScoringForRisk()` scales the scoring by the phase instruction's
`passingRisk` `r`, as for [passes](#passing-risk):
progression `0.5 + r` times, risk `1.5 − r` times, and `minKeepChance` `1.3 −
0.6 r` times, at most 1. At `r = 0.5` nothing changes. A side without a tactic
scores with factors of 1.

### Choosing

`chooseOnBall()` adds the carries to the softmax over the valid passes and the
best valid shot (see [pass decisions](#seeded-softmax)): of the
valid carries, the best of each kind competes -- hold, turn, carry and dribble
-- not every direction, as only the best zone of the goal does for shots.
`orderCarryCandidates()` puts valid candidates first by utility, ties by kind
and subject.

The choice becomes a `CarryIntent` -- the touch's target, speed, seconds and
error factor -- his pending action, which the ball system plays in the next
step if he still has the ball at his feet. A turn, carry or dribble also starts
or continues his run (`CarryRun`, in his tactical state): its kind and target,
the opponent he dribbles past, when it began, his last touch and when he got the
ball back. A run of the same kind carries on from when it began; a dribble
always begins anew. The run belongs to his possession: `activeCarry()` ignores
it once he has lost the ball or anyone else has touched it. Holding the ball
stops him where he is.

#### When he decides

He decides only with the ball at his feet: at the decision interval, and in the
tick after he got his touched ball back. While it rolls ahead there is nothing
to decide. His own touches do not make him a new owner: the hold time of a
freshly taken ball does not apply while his run is active.

#### Looking ahead

While the ball rolls ahead of him he orients himself for his next decision
(GDD section 8.5): [perception](players.md#perception) looks along his run's gaze
instead of his facing. Until the touch's seconds have passed he looks at his
run's focus -- his best pass's receiver when he chose, or the goal if his best
shot was better, otherwise the carry's target --, then at the first of his
[look targets](#without-a-valid-option), and back at the ball
once he is `lookBackSeconds` (0.3 s) from meeting it.

#### Holding on and getting stuck

Holding the ball, or carrying it without gain — less than `minCarryDistance`
toward goal, no better shot, nobody bypassed — he
[looks round for an option](#without-a-valid-option) and, stuck for
`stuckSeconds`, plays his best pass or kicks long. A carry with a gain keeps the
look round's time; it ends only when he plays or loses the ball. So a player
who keeps gaining ground is never stopped, and one who goes round in circles
is.

### Events and diagnostics

| Event              | Fields                                   | Recorded when                        |
|--------------------|------------------------------------------|--------------------------------------|
| `DribbleAttempted` | `carrier`, `opponent`, `from`, `target`  | the first touch of a dribble leaves his foot |
| `DribbleEnded`     | `carrier`, `opponent`, `outcome`, `position` | he gets the ball back -- `beaten` if it is then farther up the pitch than the opponent, `kept` otherwise -- or loses it: `lost`; none if his touch goes in |
| `BallWon`          | ... `cause`                              | `looseTouch` for an opponent who got to the touched ball |

No event marks a single touch (there are hundreds a match); the decision trace
shows them.

The [decision diagnostic](match-engine.md#decision-diagnostics) holds every
carry candidate with its scores and the weights they were scored with, and the
outcomes `kHeld`, `kTurned`, `kCarried` and `kDribbled` with the index of the
chosen carry; its `target` is the carry's target. The
[decision trace](tooling.md#decision-trace) prints the choice with its parts and why.

### Configuration

`CarryScoringConfig` is part of `DecisionConfig` (`decisions.carrying`) and of
every replay; `validate()` rejects values outside their ranges.

| Field               | Default | Meaning                                                |
|---------------------|---------|--------------------------------------------------------|
| `enabled`           | true    | players carry the ball at all                          |
| `minConfidence`     | 0.3     | opponents remembered with less are ignored             |
| `horizonSeconds`    | 2 s     | the longest carry he plans, at his running speed       |
| `minCarryDistance`  | 2 m     | a shorter carry is none                                |
| `gapRadius`         | 20 m    | opponents this close make the gaps                     |
| `sameDirection`     | 0.98    | directions this close are one                          |
| `pitchMargin`       | 1 m     | carries and dribbles end this far inside the lines     |
| `safetySeconds`     | 0.3 s   | a carry ends where an opponent gets there this soon after him |
| `lossMarginSeconds` | 0.4 s   | the smoothing of the risk of losing the touched ball   |
| `touchSeconds`      | 0.5 s   | the time to the ball a touch is planned for            |
| `minTouchDistance`, `maxTouchDistance` | 1 m, 5 m | a touch's shortest and longest       |
| `touchShare`        | 0.5     | he plays the ball again after this share of the planned time |
| `lookBackSeconds`   | 0.3 s   | he looks back at the ball this long before he meets it |
| `turnDistance`      | 3 m     | the longest turn                                       |
| `dribbleRadius`     | 6 m     | he dribbles past opponents this close                  |
| `dribbleCone`       | 0.5     | and this nearly in the attacking direction             |
| `dribbleClearance`  | 2.5 m   | the ball passes the opponent this far to his side      |
| `dribbleBeyond`     | 1.5 m   | and meets him this far behind the opponent             |
| `maxTouchError`, `minTouchError` | 3, 0.5 | the touch's error factor at no and at full ball control |
| `maxDribbleError`, `minDribbleError` | 1.5, 0.5 | a dribble's at no and at full dribbling |
| `slowDribbleShare`, `quickDribbleShare` | 1.2, 0.8 | a dribble's planned time over a carry's |
| `shotDistance`, `fullOpening` | 25 m, 0.6 | the shot value's range and full opening      |
| `minKeepChance`     | 0.35    | candidates less likely to keep the ball are rejected   |
| `pressureRadius`    | 6 m     | the pressure on him holding the ball                   |
| `keepWeight` ... `riskWeight` | 0.8, 0.8, 0.2, 0.6, 0.15, 0.3 | the weights of the scoring        |
| `holdWeight`        | 0.5     | holding the ball's worth without pressure              |
| `turnCost`, `turnSeconds` | 0.6, 0.5 s | a turn's cost per second, and its time standing |
| `buildUp`, `progression`, `finalThird`, `attackingTransition` | see above | the phase factors |

With them, `BallPhysics::minCarrySpeedFactor` and `maxCarrySpeedFactor` (0.8,
0.95), and the players' `ballControl` and `dribbling` in `PlayerAttributes`, in
[0, 1], 0.5 by default.

### Scenarios

The [carry scenarios](scenarios.md#the-carry-scenarios) show both: in
`carry-into-space` a carrier with nobody to pass to carries the ball into open
space touch after touch, and in `dribble-past-defender` he carries it at a
covering defender and takes it past him on one side.

### What this is not

- Fatigue does not affect the touch: [fitness](players.md#fitness) slows the
  run only.
- No feint: an opponent wins a touched ball only by reaching it first, a ball at
  the feet in a [duel](duels.md#duels-on-the-ball).
- A carry does not aim to open a passing lane, and nobody carries the ball for
  a cross.
