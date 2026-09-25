# Zones and team shape

Tactics speak of wings, halfspaces, thirds, the space between the lines and
behind them. `sim-match` provides these as **derived data** (`zones.hpp`):
queries over the pitch and the current state. No simulation rule treats a zone
boundary as a hard limit -- a player a centimetre across one is not in a
different game -- so responsibilities like "provide width" or "occupy
halfspace" can be evaluated against zones without zones steering anything by
themselves (implementation plan section 6.4).

## Static zones

Static zones depend on the pitch only, and on which side is looking: lanes are
named left to right as seen by a player attacking the opponent's goal, thirds
from the side's own goal line.

| Lanes (`Lane`)                           | Across the width, from the side's left touchline |
|------------------------------------------|--------------------------------------------------|
| `leftWing`                               | the first fifth                                  |
| `leftHalfspace`                          | the second fifth                                 |
| `centre`                                 | the middle fifth                                 |
| `rightHalfspace`                         | the fourth fifth                                 |
| `rightWing`                              | the last fifth                                   |

Home attacks `+x`, so its left touchline is `y = width`; away's is `y = 0`. On the
60 × 40 m sandbox pitch every lane is 8 m wide. A position on a boundary belongs
to the lane further right, a position off the pitch to the nearest lane.

| Thirds (`Third`) | By depth from the side's own goal line |
|------------------|----------------------------------------|
| `defensive`      | the first third of the length          |
| `middle`         | the second                             |
| `attacking`      | the last                               |

`laneOf()`, `thirdOf()` classify a position; `laneRect()`, `thirdRect()` return
the zone as a `PitchRect` (`pitch.hpp`), and `laneCenterY()` a lane's centre line.

## Team shape

`measureTeamShape(state, side)` measures how the side's outfield players stand
right now, as depths from its own goal line in meters:

| Field           | Meaning                                                      |
|-----------------|--------------------------------------------------------------|
| `defensiveLine` | the deepest outfield player: the last line                   |
| `midfieldLine`  | the median outfield depth                                    |
| `frontLine`     | the highest outfield player                                  |
| `length`        | front line minus defensive line: the vertical compactness     |
| `width`, `minY`, `maxY` | the outermost outfield players across the pitch: the horizontal compactness |
| `centroid`      | the mean outfield position, in pitch coordinates              |

The goalkeeper -- the player whose slot holds `guardGoal` in his side's tactic,
see `isGoalkeeper()` -- is not an outfield player. A scripted side has no
goalkeeper, so its shape includes everyone.

## Dynamic zones

Dynamic zones follow a team's current shape:

| Query                                  | Zone                                                          |
|----------------------------------------|---------------------------------------------------------------|
| `betweenLines(opponent, shape)`        | from the opponent's defensive line to his midfield line, across the width his outfield players span: room to receive between the lines |
| `behindLine(opponent, shape)`          | from his defensive line to his goal line, full width: where runs in behind go |
| `restDefenceZone(side, ballPosition)`  | 5 to 20 m behind the ball, toward the own goal and never past it, across the centre and both halfspaces: where a side in possession keeps players against a counterattack |

The rest-defence distances are `kRestDefenceNear` and `kRestDefenceFar`; the lane
fraction is `kLaneFraction`. Like the tactic model's heights, they are sandbox
choices and documented here rather than tuned per tactic.
