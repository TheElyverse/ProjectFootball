# Fouls and cards

A foul is a [tackle](duels.md#tackles-and-blocks) that went through the man; the
foul rules turn it into a decision: a free kick where it happened or a penalty
inside the area, advantage when the fouled side keeps the ball up the pitch, and
the card the tackle earned. They are in `sim-match` (`fouls.hpp`), opt-in like
[restarts](match-engine.md#restarts) and [offside](match-engine.md#offside):
`FoulConfig::enabled` is false by default, so a foul without them only puts the
tackler off his feet, as before; the [benchmark](tooling.md#sim-benchmark)
turns them on. `FoulConfig` is part of `MatchConfig` and of every replay.

## Where fouls come from

Fouls come out of the duel, from geometry and timing, never from a roll of
their own: a lunge whose way passes within `bodyRadius` of the carrier before
it meets the ball is a foul ([the lunge](duels.md#the-lunge)). The duel records
it as `MatchState::lastFoul()` — offender, victim, tick and where the ball was
— and the foul system, after the systems that move players and the ball and
before the restarts, judges it in the same step. A clean tackle — the foot meets
the ball first — is no foul, and neither is a missed lunge. Blocks of a shot
and challenges in the air are never fouls yet.

## The decision

`judgeFoul()` reads the state before the step the foul was committed in.

**Criteria.**

- **From behind**: the tackler stood behind the victim — the dot of the
  victim's facing and the way from him to the tackler below `fromBehindDot`
  (−0.5), more than 120° off his facing.
- **Denying an obvious goal-scoring opportunity**: the foul was no farther than
  `denyingDistance` (25 m) from the goal the victim attacks, and no outfield
  player of the offender's side but the offender was nearer its goal line than
  the victim — the offender was the last defender.

**Cards** (Law 12):

| The foul                                             | Card |
|------------------------------------------------------|------|
| denies an obvious goal-scoring opportunity outside the offender's penalty area | red |
| denies one inside it — a lunge is always an attempt to play the ball | yellow |
| from behind                                          | yellow |
| any other                                            | none |

A yellow for a player already booked is a **second yellow**: he is sent off as
for a red. Each card shown costs `cardLostSeconds` (15 s) of
[added time](match-clock.md#added-time).

**The restart.**

- Inside the offender's penalty area: a **penalty**. The victim's side's player
  nearest the penalty spot stands behind the ball on it.
- Otherwise, if the victim's side still has the ball after the step — the foul
  takes the victim's pending pass or shot with it, so he keeps it unless a
  teammate's touch took it on —, at least `advantageDepth` (two thirds) up the
  pitch, and nobody is sent off: **advantage**.
  Play goes on; the card is shown at once all the same, and the decision is
  final — the referee does not come back to it.
- Otherwise a **direct free kick** where the foul was: the victim's side's
  player nearest the spot stands behind the ball, as for an
  [offside](match-engine.md#the-free-kick).

A stopped foul ends the play the offside rule watched, and costs
`freeKickLostSeconds` (10 s) or `penaltyLostSeconds` (45 s). The system records
**`FoulCalled`**: the offender's side, the offender and the victim, where it
happened, the restart (`freeKick`, `penalty` or `advantage`), the card
(`none`, `yellow`, `secondYellow` or `red`) and the two criteria, then the
restart's `RestartTaken` (`directFreeKick` or `penalty`).

## Cards

A player's `yellowCards` and `sentOff` are part of his state and of the initial
snapshot; validation holds that a player on the pitch has at most one yellow.

**Sent off**, he leaves the pitch: he stands at `benchPosition()`, five metres
beyond the touchline at `y = 0` by the halfway line, at rest, without a target,
a pending action, a memory or a tactical state, and leaves a ball he owned
free. From then on no system moves, sees or counts him — `isOnPitch()` keeps him
out of perception, movement, tactical movement, pressing and its passing
options, the carrier's looks, headers, every claim of the ball, pursuit, pitch
control, team shape, shot stopping, the offside line, restart takers and every
kickoff line-up — and every memory of him is wiped. A half does not end in the
step of a foul, so the line-up for the second half never sees him before he
went. His side plays on with ten in their slots: his slot's
responsibilities go unfilled, every other player keeps his own.

## Tests

`tests/unit/sim-match/foulsTests.cpp` covers a foul in midfield (a free kick
without a card), advantage in the attacking third, a penalty, the yellow for a
tackle from behind, the red and the yellow for a denied goal outside and inside
the area, a second yellow and the sending-off, a side playing on with a player
sent off, and the duels' clean tackles, which are never called.

At the time of writing the style benchmark sees about 0.2 to 7 fouls a side over
90 minutes — pressing, which takes carriers on most, the most — and few cards.

## What this is not

- No fouls in the air, at blocks, by holding or pushing, and no simulation.
- No caution for persistent fouling, dissent or stopping a promising attack.
- Advantage is never called back, and the free kick keeps no distance: opponents
  may stand next to the ball. How free kicks and penalties are taken is for
  their own system.
- A side down to ten does not reorganise its shape. Goalkeepers do not press,
  so no duel sends one off; one sent off in a scenario leaves his side without
  a keeper.
