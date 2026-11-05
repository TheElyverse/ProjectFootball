# Documentation

Two planning documents define the project; the topic documents describe what
exists in code, one area each.

## Planning

- [Game design document](game-design-document.md): what the game is, its
  mechanics and product vision (German).
- [Implementation plan](implementation-plan.md): architecture, roadmap and
  engineering plan (German).

## The match simulation

- [Match engine](match-engine.md): pitch geometry, the validated match state,
  the fixed-timestep loop with its commands and systems, events and decision
  diagnostics, and restarts.
- [Match clock](match-clock.md): halves, the duration and its ninety-minute
  clock, added time, and full time.
- [Players](players.md): player movement, fitness, perception, and the spatial
  queries every decision asks.
- [Ball](ball.md): how the ball rolls, flies and bounces, possession, and
  reception, interceptions and loose balls.
- [On the ball](on-ball.md): pass candidates and decisions, passing, shot
  decisions, shooting, and ball carrying and dribbling.
- [Goalkeeper](goalkeeper.md): positioning and sweeping, shot stopping, and
  distribution.
- [Duels](duels.md): duels on the ball, and aerial duels and headers.
- [Team tactics in the match](team-tactics.md): phases, pitch control, zones and
  team shape, desired regions, off-ball movement, the defensive shape, and
  pressing.
- [Tactics](tactics.md): the tactic model, its file format, and the tactical
  identities under `data/tactics/`.

## Running, recording and measuring

- [Scenarios](scenarios.md): the named, reproducible match setups and the
  golden scenarios.
- [Tooling](tooling.md): the replay format, decision traces, the debug viewer,
  match analytics, and `sim-benchmark`.
- [Apps](apps.md): the website and the manager UI spike in Unreal.

## Writing documentation

Describe a new system in the topic document it belongs to, as a chapter of its
own if it needs one, and add a new document only for a new area. Link to a
chapter by its anchor, for example `docs/goalkeeper.md#shot-stopping`, from code
comments as well as from other documents. Design and planning documents are
written in German, the topic documents in English.
