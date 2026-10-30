# Scenarios

The named, reproducible match setups that `sim-cli` runs and the acceptance
tests check, and the golden scenarios that pin whole situations down.

- [Scenario catalogue](#scenario-catalogue)
- [Golden scenarios](#golden-scenarios)

## Scenario catalogue

A scenario is a named, reproducible match setup: a fixture, the configuration
of the standard systems, and scripted commands. The caller gives the seed;
everything else is fixed, so a scenario name and a seed identify a match. The
catalog is in `sim-match` (`scenarios.hpp`); `sim-cli --scenario <name>` runs
one.

| Name           | Setup                                                             |
|----------------|-------------------------------------------------------------------|
| `kickoff`      | the seven-a-side kickoff fixture, the ball free on the center spot |
| `rolling-ball` | the kickoff fixture with the ball rolling at (8, 3) m/s            |
| `m0-acceptance`| all 14 players on scripted runs with target changes, rolling ball |
| `pass-chain`   | home player 1 on the ball, teammates in a zigzag ahead, no opponent in reach |
| `intercepted-pass` | home player 1's only option is a risky pass past away player 8 |
| `no-passing-option` | home player 1 on the ball, every teammate behind him out of sight, looks round and plays back |
| `no-option-forced` | home player 1 on the ball, every pass he sees cut out, plays the best after looking round ([pass decisions](on-ball.md#without-a-valid-option)) |
| `idle-carrier-challenged` | home player 1 stands on the ball, away does not press and still goes at him ([pressing](team-tactics.md#a-carrier-who-stands-on-the-ball)) |
| `clear-chance` | home player 1 eight meters out, only the keeper to beat ([shot decisions](on-ball.md#shot-decisions)) |
| `goal-kickoff` | the clear chance with tactics and restarts, the kickoff after the goal ([restarts](match-engine.md#the-kickoff)) |
| `hopeless-angle` | home player 1 on the goal line wide of the post, a teammate free |
| `blocked-lane` | home player 1 twelve meters out, a defender in the way, a teammate free |
| `early-cross` | home player 1 wide in the final third crosses for a striker running into the box ([crossing](on-ball.md#crossing)) |
| `cut-back` | home player 1 wide near the goal line cuts the ball back to a teammate at the edge of the area |
| `cross-keeper-claims` | home player 1 crosses against a keeper quicker than he imagines, who claims it |
| `curl-around-wall` | home player 1 sixteen meters out, a wall of three in front of him, curls the ball around it ([shot types](on-ball.md#shot-types)) |
| `chip-advanced-keeper` | home player 1 sixteen meters out, the keeper eight meters off his line, chips him |
| `keeper-arc` | home player 1 carries the ball across in front of away's goal, the keeper moves with it ([goalkeeper](goalkeeper.md#positioning-and-sweeping)) |
| `keeper-sweep-claim` | a through ball in behind away's line, the keeper first to it |
| `keeper-sweep-leave` | a through ball in behind away's line, home's striker first to it |
| `keeper-build-up` | home's keeper holds the ball and plays out to a free centre back ([goalkeeper distribution](goalkeeper.md#distribution)) |
| `keeper-turned` | home's keeper holds the ball facing his own goal, turns and plays out |
| `keeper-long-kick` | home's keeper on the ball under a high press goes long |
| `goal-kick` | home plays the ball over away's goal line, away's keeper takes the goal kick |
| `corner` | eleven a side, away played the ball over its goal line: home's corner from its plan ([set pieces](#the-set-piece-scenarios)) |
| `throw-in` | eleven a side, away played the ball over the touchline: home's throw-in from its plan |
| `tactic-match` | the reference tactic against itself, lined up for home's kickoff   |
| `tactic-match-eleven` | the eleven-a-side reference tactic against itself on the standard pitch, lined up for home's kickoff |
| `transition-3v2` | home wins the ball in midfield, three attackers against two defenders ([golden](#golden-scenarios)) |
| `isolated-winger` | home plays out to an isolated winger, away presses on the trigger |
| `touchline-trap` | away's receiver faces his own goal at the touchline, home presses four players |
| `lone-press` | the touchline trap with one home presser instead of four |
| `run-behind-line` | home's striker level with away's defensive line, space behind it |
| `carry-into-space` | home player 1 on the ball, nobody to pass to, open space ahead ([ball carrying](on-ball.md#ball-carrying)) |
| `dribble-past-defender` | home player 1 carries the ball at a covering defender and past him |
| `duel-ball-won` | a strong centre back takes on a weak carrier with his back to goal and wins the ball ([duels](duels.md#duels-on-the-ball)) |
| `duel-ball-kept` | a strong carrier with his back to goal shields the ball from a weak centre back and gets away |
| `duel-loose-ball` | an even duel knocks the ball loose, a teammate of each close by |

Unless its section says otherwise, a scenario uses the 60 × 40 m example pitch
and the default `MatchConfig` (30 Hz). `sim-cli --list-scenarios` prints the
catalog.

### The M0 acceptance scenario

`m0-acceptance` is milestone M0's integration scenario. It starts from the
kickoff fixture with the ball rolling at (9, 4) m/s. Every player gets six
waypoints, one every six seconds, staggered by four ticks per player, so target
changes spread over two seconds. The waypoints follow a fixed arithmetic pattern
over the whole pitch, so most changes reach a player mid-run. At tick 600 player
1 is sent behind a goal line and player 14 past a corner; both targets are moved
onto the pitch. After 36 seconds everyone has reached his last waypoint.

`tests/acceptance/m0AcceptanceTests.cpp` runs it for three minutes and checks:

- every player covers ground, and targets change while players run;
- nine minutes of simulation never produce a non-finite value, a ball off the
  pitch, a speed above a player's limit or a target off the pitch;
- two runs give identical checkpoint hashes, and so does playback of the replay
  after a JSON round trip;
- advancing in batches of 1, 7, 30 or 451 ticks, each continued from a copy of
  the simulation, gives the hashes of one uninterrupted run;
- the final state hash equals a pinned value on every platform CI builds on.

CI runs these tests in their own step (`ctest --preset ci --label-regex
acceptance`).

### The P1 passing scenarios

The P1 scenarios exercise the whole passing pipeline — perception, pass
candidates, the seeded decision, execution, the ball's flight and reception —
without scripted runs or passes. Each is a hand-placed fixture: home players get
ids 1 to 7, away players 8 to 14, and the ball starts at player 1's feet and is
given to him at tick 0. After that only the standard systems act.

Every hand-placed fixture — these, and the shot, goalkeeper, distribution and
stuck-carrier scenarios below — sets up one pass or shot decision, so it
switches [carrying](on-ball.md#ball-carrying) off
(`decisions.carrying.enabled`): the carrier passes, shoots or keeps the ball.
The [carry scenarios](#the-carry-scenarios) switch it on.

- **`pass-chain`**: home stands in a zigzag up the pitch (x = 8, 20, 20, 32, 44,
  44, 54 m), everyone facing the away goal, so every carrier sees teammates
  ahead. Away stands along the home goal line, behind every pass. Home passes
  the ball on from player to player.
- **`intercepted-pass`**: player 1 stands at (20, 20) facing the away goal. His
  only visible teammate is player 2, 20 m ahead at (40, 20); the others stand
  behind him, out of sight. Away player 8 waits at (34, 26.0), 6.0 m off the
  lane: close enough that the pass barely stays valid (completion just above
  `minCompletion`), far enough that player 1 plays it. Player 8 goes after the
  ball and, for most seeds, takes it. This checks that the risk estimate means
  something: the risky passes it lets through really are lost.
- **`no-passing-option`**: player 1 stands at (30, 20) facing the away goal,
  every teammate behind him beyond his awareness radius. At first he sees no one
  to pass to and decides "no valid option". He
  [looks over his shoulder](on-ball.md#without-a-valid-option), finds his
  teammates and plays back to one. Away waits in its own half.

#### Acceptance checks

`tests/acceptance/p1AcceptanceTests.cpp` runs each scenario for twenty seconds
with seed 7. On every tick it checks the possession invariants: a controlled
ball sits exactly where the [carry rule](ball.md#possession) puts it and moves
with its owner; a pending pass belongs to the player on the ball; possession
changes only through a chain of `PossessionChanged` events; the owner plays
passes, teammates receive them and opponents intercept them. Then:

- `pass-chain`: at least three passes in a row are decided, played and received
  by home players — with seed 7 and with each of the seeds 1 to 50;
- `intercepted-pass`: player 1's only candidate is valid with an interception
  risk above 0.5, he plays it, and player 8 intercepts it; over the seeds 1 to
  50 at least 70 % of these passes are lost (a statistical guardrail, not an
  exact value);
- `no-passing-option`: player 1's first decision finds no candidate because he
  sees only opponents, and he looks toward his teammates behind him; his next,
  within a second, is a pass to one of them, which a home player receives;
- two runs, and playback of the replay after a JSON round trip, agree on every
  state and event hash, and collecting decision diagnostics changes neither;
- the final state and event hashes are pinned.

CI runs them with the other acceptance tests.

#### Watching them

Run a scenario with debug frames and watch it in the
[debug viewer](tooling.md#debug-viewer):

```sh
./build/debug/apps/sim-cli/sim-cli --scenario intercepted-pass --seed 7 --ticks 600 \
  --replay-out intercepted.json --frames-out frames.json
cd apps/sim-viewer && pnpm install && pnpm run build && pnpm run serve ../../frames.json
```

What to look at:

- **`pass-chain`**: click the player on the ball (yellow ring). His vision cone
  covers the teammates ahead. Step to the tick of his decision (the event log
  shows the passes) to see every candidate line, the chosen one yellow, and the
  scores in the panel. Step on to see the receiver take the ball.
- **`intercepted-pass`**: select player 1 and step to tick 19. The panel shows
  the decision from the step from tick 18: one valid pass to player 2 with a
  risk of about 0.64. After the pass, select player 8 to follow his run; the
  event log shows the interception at tick 70.
- **`no-passing-option`**: select player 1. His vision cone points at the away
  side, his teammates are behind him without an observation circle, and his
  first decision reads "no valid option". Step on: the cone swings round to his
  teammates while his facing stroke stays, and he plays back.

### The shot scenarios

Home player 1 has the ball near the away goal. Away plays the reference tactic,
so away player 8 is its keeper; home plays without a tactic, and its other
players wait in their own half.

- **`clear-chance`**: player 1 eight meters out, straight in front of the goal,
  the keeper on his line, every away outfield player behind him.
- **`hopeless-angle`**: player 1 on the goal line six meters wide of the post,
  looking infield. The goal is a sliver from there; teammate 2 waits free at the
  edge of the area.
- **`blocked-lane`**: player 1 twelve meters out, away player 9 two meters in
  front of him, square in the way; teammate 2 free to his left.
- **`curl-around-wall`**: player 1, of perfect technique, sixteen meters out to
  the right of the goal as he faces it; away players 9 to 11 stand in a wall
  9.15 m in front of him, square to his line to the goal's centre and covering
  its near side, the keeper on his line toward the far post.
- **`chip-advanced-keeper`**: player 1 sixteen meters out in front of the goal,
  the keeper eight meters off his line, every other away player behind player 1.

Both decide after two perception updates (`minHoldSeconds` 0.2 s) rather than
half a second, before the wall or the keeper has moved far.

- **`goal-kickoff`**: the clear chance with a
  [kickoff](match-engine.md#the-kickoff) after it. Both sides play the reference
  tactic — home's with its slots in the order its players stand, so player 1 is
  the striker and player 7 the keeper — and restarts are on. With seeds 1, 7
  and 8, for example, player 1 scores within the first second and a half,
  after his `minHoldSeconds`; in the next step both sides
  stand in their formations in their own halves, and away's striker, player 14,
  has the ball on the centre spot:

  ```sh
  ./build/debug/apps/sim-cli/sim-cli --scenario goal-kickoff --seed 1 --ticks 300 --frames-out frames.json
  ```

`tests/acceptance/shotScenarioTests.cpp` looks at player 1's first decision in
each of 100 seeds. At the clear chance he shoots at least 90 times. From the
hopeless angle every shot is `too narrow` and he always passes. Past the blocked
lane he passes more often than he shoots, with the middle of the goal at least
half blocked in at least 90 seeds. At the wall every driven shot is `blocked`,
the best shot is curled, at least 90 seeds curl it and at most 20 shots come
off the wall. Against the advanced keeper the best shot is a chip, more often
saved driven, at least 90 seeds chip him and at least 30 score. Both replay
with the same events. The clear chance is also followed to its end:
the first shot is a goal in 55 to 90 of the seeds
([shooting](on-ball.md#shooting)).

### The cross scenarios

Home player 1 has the ball wide on the right in away's final third; away keeps
goal with the reference tactic, home plays without one, and the home players a
scenario sends into the box get their movement targets in the first step.
Player 1 decides after two perception updates (`minHoldSeconds` 0.2 s).

- **`early-cross`**: player 1 sixteen meters from the goal line by the
  touchline; striker 2 runs from the edge of the area to the penalty spot, an
  away centre back level with him on the far side.
- **`cut-back`**: player 1 three meters from the goal line, looking into the
  box; teammate 2 waits at the cut-back point, two away defenders stand on
  their six-yard line.
- **`cross-keeper-claims`**: the early cross against a keeper quicker off his
  line (9 m/s, 8 m/s²) and surer of hand than the crosser imagines anyone --
  he knows nobody's attributes.

`tests/acceptance/crossScenarioTests.cpp` plays each for three seconds over 30
seeds. The early cross is crossed at least 20 times, always in the air and for
the striker, who gets to it first at least 8 times and heads it at least 3. The
cut-back is cut back along the ground at least 25 times and reaches teammate 2
at least 25. The quick keeper claims the cross in at least half the seeds and
at least 5 more often than the ordinary keeper of the early cross. Two runs of
each give the same events.

### The goalkeeper scenarios

Away plays the reference tactic, so away player 8 keeps goal; home plays without
a tactic. They show the
[goalkeeper](goalkeeper.md#positioning-and-sweeping)'s own model:

- **`keeper-arc`**: home player 1 starts on the touchline 27 m in front of
  away's goal with the ball and carries it to the other touchline, beyond the
  25 m a shot is considered from. So that only the ball moves the keeper, the
  carrier holds the ball for a minute (`decisions.minHoldSeconds`), and away's
  outfield players neither press, mark nor track (`defensive.pressRadius`,
  `markRadius` and `trackRadius` of 0.1 m).
- **`keeper-sweep-claim`**: home player 1 on the halfway line plays a through
  ball at tick 1 between away's centre backs to 12 m in front of away's goal;
  home's striker 2 starts wide and far from it.
- **`keeper-sweep-leave`**: the same ball, with home's striker standing on its
  way, 15 m in front of the goal.

In both sweep scenarios away's keeper judges perfectly (`keeperAnticipation`
1), so whether he comes is his model's call, not a misjudgement.
`tests/acceptance/goalkeeperScenarioTests.cpp` checks each over ten seeds. In
the arc the keeper stands off his line, on the ball's side of the goal, and once
the ball has stopped, within 0.75 m of the bisector of his goal's angle. In the
claim his first call is to come, and he is first to the ball. In the leave he
stays home and in his area, and the striker has the ball.

### The distribution scenarios

They show what a [goalkeeper](goalkeeper.md#positioning-and-sweeping) does with
the ball ([goalkeeper distribution](goalkeeper.md#distribution)):

- **`keeper-build-up`**: home plays the reference tactic with its slots in
  order, so player 1 is its keeper, at his goal with the ball in his hands from
  the start. His tactic's directness is 0.1; the tactic is named
  "reference, plays out". His centre backs stand wide and free. Away plays
  without a tactic and stands still: a meter in front of his holding
  midfielder, in the lane from the keeper, and a meter off his wingers and
  striker.
- **`keeper-turned`**: `keeper-build-up` with the keeper facing his own goal, as
  after taking a ball that rolled back to him.
- **`keeper-long-kick`**: the same home side, its keeper with the ball at his
  feet and the reference tactic's directness of 0.5. Away stands in the lanes to
  his centre backs and holding midfielder, a forward four meters to his side,
  out of the way of a long ball, and its last two defenders deep behind home's
  wingers and striker, who wait beyond the longest ground pass.
- **`goal-kick`**: away plays the reference tactic, home without one; restarts
  are on. Home player 1, wide of away's goal, plays the ball over its goal line
  at tick 1, and away's keeper, player 8, takes the goal kick.

`tests/acceptance/distributionScenarioTests.cpp` checks each over ten seeds: the
keeper throws to a centre back along the ground, kicks long to a winger or the
striker through the air, and takes the goal kick from his goal area.

### The stuck-carrier scenarios

They show what a carrier without a valid option does
([pass decisions](on-ball.md#without-a-valid-option)), and what the other side
does about a carrier who stands on the ball
([pressing](team-tactics.md#a-carrier-who-stands-on-the-ball)). Home plays
without a tactic, so its players stand still.

- **`no-option-forced`**: player 1 at (30, 20) facing the away goal, every
  teammate ahead of him with an away player in the lane two meters in front,
  nobody behind him. Every pass he sees is too likely to be cut out. He looks to
  one side and the other, and after `stuckSeconds` plays the best pass he
  rejected.
- **`idle-carrier-challenged`**: player 1 at (46, 20) with his back to away's
  goal, every teammate more than a long pass away. `stuckSeconds` is out of
  reach, so he never acts on his own. Away plays the reference tactic without
  any pressing: a centre back, player 9, a meter and a half goal-side of him,
  its forwards far off in home's half.

`tests/acceptance/stuckCarrierScenarioTests.cpp` checks each over ten seeds. In
`no-option-forced` every decision before the last looks for an option, first
side and other side in turn, and the last plays his best rejected pass between
`stuckSeconds` and a quarter of a second more after the first. In
`idle-carrier-challenged` nobody goes at him before `idleSeconds`; then player 9
does and takes him on in a [duel](duels.md#duels-on-the-ball). Both, and
`no-passing-option`, run alike every time.

### The carry scenarios

They show a carrier with nobody to pass to carrying the ball
([ball carrying](on-ball.md#ball-carrying)). Home player 1 stands at (20, 20)
facing away's goal with carrying on. Every teammate stands behind him, each with
an away player a meter and a half in front toward player 1, so every pass is too
likely to be cut out. Neither side has a tactic; everyone else stands still.

- **`carry-into-space`**: away's remaining player stands on its goal line; the
  space in front of player 1 is open. He carries the ball into it, touch after
  touch.
- **`dribble-past-defender`**: away player 8 covers the middle at (38, 20).
  Player 1 carries the ball at him and, close to him, takes it past him on one
  side.

`tests/acceptance/carryScenarioTests.cpp` checks over ten seeds that in
`carry-into-space` his first decision is a carry, he touches the ball on at
least three times, never loses it, and has it at least ten meters up the pitch
after four seconds. Over twenty seeds, in `dribble-past-defender` he dribbles at
player 8 in at least ten, every dribble ends, and at least half of his attempts
beat him, with the ball beyond the defender. Both run alike every time.

### The duel scenarios

They show a [duel](duels.md#duels-on-the-ball) on the ball and how `strength`
and `ballControl` tilt it. Each is `idle-carrier-challenged` with other players:
home player 1 stands on the ball at (46, 20) with his back to away's goal, and
after `idleSeconds` away's centre back, player 9, takes him on. Home plays
without a tactic.

- **`duel-ball-won`**: a weak carrier with a poor touch (strength 0.1, ball
  control 0.2) against a strong centre back (strength 1). Shielding, he is
  pushed off his feet and off the ball; turning away, he is tackled.
- **`duel-ball-kept`**: a strong carrier with a fine touch (strength 1, ball
  control 1) against a weak centre back (strength 0.1). He shields the ball,
  pushes the defender off balance and gets away or plays it on.
- **`duel-loose-ball`**: an even duel, the carrier with a fine touch (ball
  control 1), so a foot on the ball seldom takes it cleanly. Home player 2 waits
  beyond the centre back, who stands in the way of a pass to him, and away
  player 10 a few meters off on the other side: a ball knocked loose is theirs
  to chase too.

`tests/acceptance/duelScenarioTests.cpp` plays each over thirty seeds; nearly
all have a duel. No duel ends in the step it starts in, and each ends between
the players who started it. Away has the ball after the first duel in at least
21 of the `duel-ball-won` runs (25 at the time of writing); home in at least 12
of the `duel-ball-kept` runs (15), and three times as often as in
`duel-ball-won`. In `duel-loose-ball` at least 8 duels (12) end with the ball
loose, and a third player gets to it first after at least 2 of them (5). All
three run alike every time.

### The set-piece scenarios

`corner` and `throw-in` start from the eleven-a-side reference's kickoff
line-up with the ball at rest on away's goal line by the corner flag, or on the
touchline at 65 % of the length, last touched by away's last outfield player:
home's set piece, set up and taken from its plan. Together with `goal-kick`
they are the [set-piece scenarios](set-pieces.md#scenarios),
`tests/acceptance/setPieceScenarioTests.cpp`.

### The tactic match

`tactic-match` is the fixture two tactics play on: the players of the
seven-a-side kickoff fixture with a tactic on each side, lined up for a
[kickoff](match-engine.md#the-kickoff) by home — each side in its tactic's base
shape in its own half, the ball on the centre spot — with the standard
configuration and a `GiveBallCommand` at tick 0 to home's taker, the striker
nearest the centre spot -- player 7 of every seven-a-side preset, player 10 of
the eleven-a-side reference. `makeTacticMatch()` builds it for any two tactics
of the same size: seven-a-side ones on the sandbox, eleven-a-side ones on the
standard pitch from the [eleven-a-side kickoff fixture](match-engine.md#the-eleven-a-side-kickoff-fixture).
`tactic-match` uses the reference tactic on both sides, `tactic-match-eleven`
the eleven-a-side one; `tests/acceptance/elevenASideTests.cpp` plays two
minutes of it, valid and moving, replays it through the file format with
identical tick and event hashes, and pins its final hashes. The CLI swaps in
tactic files:

```sh
./build/debug/apps/sim-cli/sim-cli --home-tactic data/tactics/pressing.json \
  --away-tactic data/tactics/counter.json --seed 3 --ticks 1800 --frames-out frames.json
```

`--home-tactic` and `--away-tactic` imply `--scenario tactic-match`, and go with
`tactic-match-eleven` too; a side without one plays the reference tactic of the
other's size. The replay records the tactics, so it
plays back without the files. See
[tactical identities](tactics.md#tactical-identities) for the presets.

### P3 acceptance

Milestone P3 — the eleven-a-side baseline — is accepted on whole matches: the
three [tactical identities](tactics.md#tactical-identities) on the standard
pitch with every rule on — [restarts and set pieces](set-pieces.md),
[offside](match-engine.md#offside), [fouls and cards](fouls.md) — from the
kickoff to [full time](match-clock.md#full-time), half time and added time
included. It sits beside the [M0](#the-m0-acceptance-scenario) and P1
scenarios above and the [P2 style benchmark](tooling.md#p2-acceptance).

**In the suite** (`tests/acceptance/p3AcceptanceTests.cpp`, Debug):

- pressing against counter, possession against pressing and counter against
  possession, seeds 1 and 2, six-minute matches: every step valid, no
  non-finite value, full time reached within `maxMatchTicks()`, and the test
  fails at the first stretch of 30 s without the ball changing owner or of 25 s
  with the ball dead for a set piece (11.2 s and 8 s at most when measured).
  Over the six matches every kind of set piece is taken — kickoffs, throw-ins,
  corners, goal kicks and free kicks — and offside is called; fouls are rare in
  matches this short, and the Release run checks them;
- pressing against counter, seed 3, two minutes: its replay plays back with
  every checkpoint verified, and its final state and event hashes are pinned.

**In CI** (`tests/benchmark/p3Acceptance.cmake`, Release, the `style-benchmark`
job's `ctest -L benchmark`): the round robin of the three identities, one whole
90-minute match per pairing with seed 1, every rule on, one sampled replay per
pairing. It must finish within 45 s per match and 300 s for the nine together —
the sum of their times —, every match must reach full time without a player
owning the ball longer than 20 s at a time (9 to 12 s measured), every sampled
replay must reproduce, each style's mean per match over its six sides must
fall inside the bands below — every side with a value —, and pressing must
commit at least 8 fouls and catch at least 2 offsides a match, so that a rule
that stops being called fails although the offsides band starts at 0. At
the time of writing the nine matches took 12.2 s each on average, 13.5 s at
most and 110 s together; the test took 336 s, most of it the nine replays,
each recorded and played back one after the other.

**The full run** for local use plays twenty matches per pairing and five seeds,
900 matches — about half an hour with eight jobs —; compare each style's
`styleSummary` means with the bands below:

```sh
for seed in 1 2 3 4 5; do
  ./build/release/apps/sim-benchmark/sim-benchmark --style data/tactics/possession.json \
    --style data/tactics/counter.json --style data/tactics/pressing.json \
    --matches 20 --minutes 90 --seed $seed --jobs 8 --out p3-$seed.json
done
```

**The guardrails.** The bands guard the calibrated baseline: they fail a
change that stops the shooting, the passing or a rule, or that floods the match
with them, not one that only shifts the balance. They are set from the reduced
run over seeds 1 to 5 — each style's mean per match over its six sides —, with
room of about half the measured value below and above, since six sides are
few. Real football's values per side and match are given for comparison.

| Metric | Band | Measured, seeds 1–5 | Real football | Reasoning |
|---|---|---|---|---|
| `goals` | 0.4 – 7 | 0.8 – 4.7 | about 1.4 | 2.9 over all styles, about twice real football's; most of them pressing's (4.2 a match against counter's 1.2) |
| `shots` | 3 – 24 | 5.8 – 15.8 | about 12 | 10.8 over all styles; pressing shoots most, as its identity promises, counter least |
| `passCompletion` | 0.68 – 0.86 | 0.74 – 0.79 | about 0.80 | a little low: long balls and clearances arrive less often |
| `possessionShare` | 0.30 – 0.70 | 0.36 – 0.57 | 0.3 – 0.7 | plausible; possession and pressing have the most, counter the least. It guards little: shares sum to 1 per match, and the mirror pairing holds two of a style's six sides at 0.5 |
| `fouls` | 1 – 24, pressing at least 8 | 1.8 – 15.5 | about 11 | 8.9 over all styles; pressing's lunges commit the most, counter's deep block few |
| `offsides` | 0 – 8, pressing at least 2 | 0.3 – 4.5 | about 2 | pressing's high line catches the most |
| `corners` | 0.4 – 6 | 0.8 – 3.5 | about 5 | too few: shots and crosses are rarely turned behind |
| `throwIns` | 4 – 30 | 7.7 – 19.5 | about 20 | 13.1 over all styles; still too few |
| `goalKicks` | 1.5 – 9 | 3.5 – 6.0 | about 7 | a little low |
| `freeKicks` | 1.5 – 27 | 2.8 – 18.0 | about 12 | the fouls' and offsides' free kicks |
| `penalties` | 0 – 1 | 0.0 – 0.7 | about 0.15 | plausible |

**The calibration** (#232) brought the baseline there from 22 goals, 47 shots,
2.4 fouls and 2.5 throw-ins a side — measured over 27 matches, three seeds of
the round robin, per side and match:

| Metric | Before | After | Real football |
|---|---|---|---|
| goals | 22 | 2.7 | 1.4 |
| shots, on target | 47, – | 11.0, 6.4 | 12, 4.5 |
| passes, completion | 1300, 0.69 | 790, 0.76 | about 480, 0.80 |
| fouls, free kicks | 2.4, – | 9.3, 11.0 | 11, 12 |
| corners, throw-ins, goal kicks | –, 2.5, – | 2.4, 12.9, 4.6 | 5, 20, 7 |

Most of it came from flaws, not from turning numbers:

- A presser within a metre of the carrier took every pass and touch off his
  foot the moment it was played, even standing behind him: a ball that leaves a
  player is no contact now ([reception](ball.md#reception-interceptions-and-loose-balls)).
  Half the passes under close pressure went that way, and the turnovers fed
  three times real football's possessions with chances.
- The passer judged a presser's reach by his arrival time less the control
  radius at full speed, so a presser a metre away still needed a fifth of a
  second: he judged three in four such passes safe where half arrived
  ([interception risk](on-ball.md#pass-candidates)).
- A forward beside the keeper took his punt off his foot: nobody may stop the
  keeper releasing the ball now (Law 12, [goalkeeper](goalkeeper.md#positioning-and-sweeping)).
- The keeper stood high whenever his side had the ball and was caught 10 m off
  his line by a turnover and a long shot ([where he stands](goalkeeper.md#where-he-stands)).
- Receivers stood free in the box: near their goal the defenders now pick up
  the men about the ball ([defensive shape](team-tactics.md#defensive-shape)).
- The ball hardly left the pitch: defenders pressed near their goal now
  [clear](on-ball.md#clearances) it, outfield players kick
  [long](on-ball.md#long-balls) from their own half, a pass an opponent only
  stretches for comes off him
  ([reception](ball.md#reception-interceptions-and-loose-balls)), and a mistimed
  header glances a cross on ([the header](duels.md#the-header)).

The rest is set to real football's rates: the strike strays more in open play
than the shooter judges, a penalty is placed, a cross errs less than a shot
([the strike](on-ball.md#the-strike)); the keeper reads and reaches better
([shot stopping](goalkeeper.md#shot-stopping)); a carrier keeps a new ball a
second, duels knock the ball loose faster ([duels](duels.md#duels-on-the-ball)),
and set pieces take longer to set up ([the delivery](set-pieces.md#the-delivery)),
so the ball is dead a fifth of the match.

Still off: about twice real football's goals, most of them pressing's; too
many passes; too few corners, throw-ins and fouls. They share a cause: per
minute of the ball in play the match is about half as eventful as real
football — half the shots, fouls, throw-ins and corners — while it passes as
often. Real football's 35 to 40 dead minutes would bring the passes down to
real football's, and these events with them, unless the play gets livelier. The P2 identities hold
([P2 acceptance](tooling.md#p2-acceptance)); two isolated tests switch the
defenders' pick-up near their goal off, where it would blur what they measure:
the coordinated press and the free kick's wall; the press also switches the
clearances off.

The longest a player owns the ball is guarded by the
[P2 style benchmark](tooling.md#p2-acceptance) (under 20 s; 9 to 12 s in whole
matches).

**The performance budget** of a whole match with every rule is the
[eleven-a-side budget](match-engine.md#performance): a 90-minute match took
13.6 to 16.2 s on one core when measured, some 330 to 400 times real time.

### Scenarios are versioned

A scenario is a fixture: changing its layout, configuration or commands changes
every match played from it and every state hash recorded against it. Replays do
not depend on the catalog — they carry their own initial state and commands —
but tests and docs that name a scenario do. Change a scenario on purpose, update
this chapter in the same commit, and prefer adding a scenario to changing one.

### Adding a scenario

Add a function that returns the `MatchSetup`, an entry to the catalog in
`scenarios.cpp`, and a row to the table above. `scenariosTests.cpp` checks that
every scenario has a unique name, builds a valid setup that starts on the pitch,
and runs.

## Golden scenarios

The P2 golden scenarios are short, hand-placed seven-a-side situations in which
one tactical behaviour must show. They stay seven a side when the tactical
identities move to eleven: each plays the seven-a-side reference tactic or a
pressing tactic of its own built from it (`goldenPressingTactic()`), not the
identity files, so it still isolates the behaviour it was written for on a
pitch small enough to read. Each is a [scenario](#scenario-catalogue) in
the catalog — versioned like every fixture, and runnable and watchable with
`sim-cli`. `tests/acceptance/goldenScenarioTests.cpp` asserts its behaviour
through events and decision diagnostics over twenty fixed seeds. CI runs them
with the other acceptance tests; every golden scenario also has to survive a
replay round trip unchanged.

The builders are in `goldenScenarios.hpp`; their parameters let a test compare
variants of one situation. Players stand in tactic slot order — goalkeeper, two
centre backs, holding midfielder, two wingers, striker — home ids 1 to 7, away
8 to 14, on the 60 × 40 m sandbox pitch.

| Scenario          | Situation | Asserted, of 20 seeds (at the time of writing) |
|-------------------|-----------|-----------------------------------------------|
| `transition-3v2`  | Home's holding midfielder (4) wins the ball from away's (11) at the halfway line at tick 10; wingers 5, 6 and striker 7 face away's two centre backs, away's wingers and striker are caught upfield. Both sides play the reference tactic. | His first action within 1.5 s of the regain plays the ball forward, at least 3 m: a pass to one of the three attackers, or a carry or a dribble ([ball carrying](on-ball.md#ball-carrying)): at least 16 (19). |
| `isolated-winger` | Home's goalkeeper plays a long ball to winger 6 on the touchline, no teammate within 10 m. Home is scripted, so nobody comes to help; away presses on `isolatedReceiver` only and stays out of its pressing phase. | Away starts a press on 6 with trigger `isolatedReceiver` and at least three players: all 20. |
| `touchline-trap`  | Away's centre back passes to its holding midfielder (11), who stands at the touchline facing his own goal. Home presses on `receiverFacingOwnGoal` with intensity 1 — presser, cover and two lane blocks. | Over 100 seeds rather than 20, home's press wins the ball back within six seconds in at least 35 (40) — at least 15 more than `lone-press` (17) and 25 more than the same press in the centre (3). |
| `lone-press`      | The touchline trap at intensity 0.25: one presser, no lane blocks, no cover. | The uncoordinated comparison above. |
| `run-behind-line` | Home's holding midfielder on the ball at the halfway line; striker 7 level with away's centre backs, 20 m of space behind them. | Striker 7 chooses `runInBehind` within one second: at least 18 (20); an away defender answers with `trackRunner` on him within three seconds: at least 12 (17). |

A pass into the space behind the line is not asserted: until the match has a
through pass, passes go to where a teammate stands, not into his run. The
scenario shows the run and the defence's answer.

### Watching them

Record one with debug frames and open it in the
[debug viewer](tooling.md#debug-viewer):

```sh
./build/debug/apps/sim-cli/sim-cli --scenario touchline-trap --seed 3 --ticks 180 \
  --replay-out trap.json --frames-out frames.json
cd apps/sim-viewer && pnpm run serve ../../frames.json
```

- **`transition-3v2`**: step to tick 11; the phases above the pitch read
  `attackingTransition` and `defensiveTransition`. Select player 4 to see his
  pass candidates and the forward pass he chooses; switch on **regions** to see
  the attackers' desired regions pushed forward.
- **`isolated-winger`**: follow the long ball to player 6. The event log shows
  `away presses #6 (isolatedReceiver, 4 players)`, and the **press** overlay
  draws the presser, the lanes blocked back to the scripted defenders and the
  cover.
- **`touchline-trap`** and **`lone-press`**: compare the **press** overlay — the
  blocked lanes along the touchline against a single presser — and the event
  log's `home press ends: ballRegained`.
- **`run-behind-line`**: select player 7 to see his action candidates with
  `runInBehind` chosen, then an away centre back's `trackRunner` on him.

Or trace a player's decisions directly:

```sh
./build/debug/apps/sim-cli/sim-cli --play trap.json --trace trace.txt --trace-players 11,4
```
