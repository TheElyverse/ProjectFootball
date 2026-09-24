# Elyverse: Football

*Living Football.*

Build a club with an identity. Discover players who can change its future. Shape
how your team plays, and understand why it wins—or why it falls apart.

Elyverse: Football is a football management game in early development for PC,
with a simple ambition: **deep football, meaningful decisions, less busywork.**

## The game we're building

- **A world that keeps moving.** Clubs develop ambitions, leagues rise and fall,
  and new generations of players give every career its own story.
- **Your ideas, visible on the pitch.** Players read situations, make decisions,
  and bring their individual strengths and habits to your tactical approach.
- **A club you can run your way.** Take charge of recruitment, training, and match
  preparation, or trust your staff with clear goals and boundaries.
- **Players worth discovering.** Scouting offers an imperfect picture. Talent
  needs opportunity, and development depends on coaching, playing time, and health.
- **Results you can learn from.** Understand where a passing move broke down,
  why a press failed, and what your team could do differently next time.

## Where we are

Elyverse: Football is in pre-production and is not yet a playable management game.
Our current focus is a small football sandbox that will let us test movement,
passing, and player decisions before building the wider club experience.

### Try the sandbox

The M0 sandbox runs headlessly from the command line. After building (see
[Contributing](CONTRIBUTING.md)), run the acceptance scenario — fourteen players
on scripted runs with repeated changes of direction, and a rolling ball — record
it, and play the recording back:

```sh
./build/debug/apps/sim-cli/sim-cli --scenario m0-acceptance --seed 42 --ticks 1800 --replay-out m0.json
./build/debug/apps/sim-cli/sim-cli --play m0.json
```

Both print the simulated time and the final state hash; playback verifies that
the recording reproduces tick for tick. `--list-scenarios` shows what else there
is to run.

M0 is deliberately small. Players run to positions a script gives them, at their
top speed and acceleration, and the ball rolls and stops on the lines. There is
no perception, no decision making, no possession, passing or tackling, no
collisions, no rules and no graphics yet — those are the next steps.

Explore the [game design document](docs/game-design-document.md) for the full
vision, or follow progress through the
[GitHub milestones](https://github.com/TheElyverse/ProjectFootball/milestones).

Interested in working on the game? Start with [Contributing](CONTRIBUTING.md).
