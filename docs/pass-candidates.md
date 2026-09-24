# Pass candidates

Before a player on the ball decides, he lists his passing options and scores
each of them — from what he believes, not from the true state. Candidate
generation lives in `sim-match` (`passCandidates.hpp`) as a pure function,
`generatePassCandidates(state, carrierIndex, now, secondsPerTick, rules)`. It is
the *generate candidates* and *estimate utility* part of the decision pipeline
(implementation plan, section 6.6); choosing among the candidates and playing the
pass are separate steps.

## Only what the carrier remembers

Every input about positions comes from the carrier's
[perception](perception.md) memory:

- **Receivers.** Every teammate the carrier remembers with at least
  `minConfidence` (0.3) is a candidate. The target is where the carrier believes
  the teammate is now: `estimatePosition()` of the observation.
- **Opponents.** Only opponents he remembers with at least `minConfidence` count,
  at their estimated positions and last seen velocities. An opponent he has not
  seen does not exist for the pass, however close he really is.
- **Movement limits.** The carrier does not know anyone's attributes, so he
  imagines every player with the default limits.

Who is a teammate is known from the squad; where he is, is not.

## Scores

For a candidate at `distance` meters, the pass speed is
`planPassSpeed(distance)` (see [passing](passing.md)) and the lane is the
straight line from the ball to the target.

| Component            | Range   | How                                                                 |
|----------------------|---------|---------------------------------------------------------------------|
| `interceptionRisk`   | 0 … 1   | chance that some remembered opponent reaches the lane first         |
| `completion`         | 0 … 1   | `(1 − interceptionRisk) · receiverConfidence`                       |
| `progression`        | −1 … 1  | meters gained toward the opponent's goal line over the pitch length |
| `receiverPressure`   | 0 … 1   | `1 − d / pressureRadius` for the nearest remembered opponent at `d` from the target, at least 0 |
| `utility`            |         | `completion·w_c + progression·w_p − pressure·w_r − risk·w_i`       |

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

The scores use arithmetic and square roots only — no exponential or logistic
function — so they are identical on every platform.

**Attacking direction.** Home attacks `+x` and away `−x` (`attackingDirection()`);
the sandbox has no halves.

## Valid candidates

A candidate can be played unless it is

| Rejection     | Reason                                                           |
|---------------|------------------------------------------------------------------|
| `kTooClose`   | closer than `minPassDistance` (2 m)                              |
| `kTooFar`     | farther than `maxPassDistance` (35 m)                            |
| `kOutOfReach` | even the planned speed stops before the target                   |
| `kUnlikely`   | `completion` below `minCompletion` (0.35)                        |

Invalid candidates stay in the list with their reason, so a decision can be
explained. When no candidate is valid, the carrier has no pass to play and keeps
the ball.

## Order

Valid candidates come first, by descending utility, then the invalid ones; equal
utilities are ordered by receiver id. The same state always gives the same list,
with every score component visible.

## Configuration

`PassScoringConfig`:

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

## What this is not

Passes go to where the receiver is believed to be, not into his run or into
space; there is no tactical fit, no personality and no cognitive error in the
estimates yet.
