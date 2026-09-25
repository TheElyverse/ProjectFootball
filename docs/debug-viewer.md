# Debug viewer

The debug viewer shows a match in 2D: the pitch, both teams, the ball and who
has it, and for a selected player what he is running to, what he sees, what he
remembers and how he decided his last pass. It is a static web page
(`apps/sim-viewer`, TypeScript and Canvas) that plays back **debug frames**
written by `sim-cli`.

The viewer never simulates. `sim-cli` runs the match headlessly, exactly as it
does without the viewer, and writes one frame per tick; the page only reads that
file. Pausing, stepping, scrubbing, changing the speed or selecting a player
changes what is on screen, never what happens in the match.

## Watch a match

Record a scenario with frames next to its replay:

```sh
./build/debug/apps/sim-cli/sim-cli --scenario m0-acceptance --seed 42 --ticks 900 \
  --replay-out replay.json --frames-out frames.json
```

Then build and serve the viewer (Node.js 22 or newer, and npm):

```sh
cd apps/sim-viewer
npm install
npm run build
npm run serve -- ../../frames.json
```

and open the printed URL, `http://localhost:8080/?frames=frames.json`. Without a
file argument, `npm run serve` serves the page alone and **Frames** loads a file
from disk. The page needs a server rather than `file://` because browsers do not
load JavaScript modules from files; `scripts/serve.mjs` is a few lines of
`node:http` bound to `127.0.0.1`, and `--port <n>` changes the port.

Collecting frames does not change the match: the replay and the final state
hash are the same with or without `--frames-out`, and every frame carries the
state hash of its tick, so a frame can be matched against a replay checkpoint.

## What it shows

| On the pitch                   | Meaning                                                      |
|--------------------------------|--------------------------------------------------------------|
| blue / red disc with a number  | a home / away player and his id; the white stroke is his facing |
| yellow ring                    | the ball's owner                                             |
| white disc                     | the ball                                                     |
| dashed yellow line             | a pending pass, from the passer to its target               |
| dashed white line and cross    | a movement target (selected player, or all with **all targets**) |

Clicking a player selects him; Escape or a click on empty grass clears it. For
the selected player the pitch adds:

- his **vision cone**, `viewDistance` deep and `fieldOfViewDegrees` wide around
  his facing, and his **awareness radius**, the circle in which he notices
  players behind him ([perception](perception.md));
- his **observations**: a dotted circle where he remembers each player and the
  ball, fainter as its confidence falls;
- his **latest pass decision**: a line from where he decided to every candidate
  target, green if valid, grey and dashed if rejected, the chosen pass yellow
  and thick ([pass candidates](pass-candidates.md), [pass decisions](pass-decisions.md));
- his **latest action decision** without the ball: a dashed white line and ring
  at every candidate target, the chosen one yellow
  ([off-ball movement](off-ball-movement.md), [defensive shape](defensive-shape.md)).

In a match with tactics, the checkboxes below the controls switch the tactical
overlays, drawn under the players:

| Overlay           | On the pitch                                                  |
|-------------------|---------------------------------------------------------------|
| **pitch control** | each cell of the latest [pitch control](pitch-control.md) grid in the colour of the side that controls it, the stronger the clearer its control |
| **zones**         | dotted lines between the five lanes, dashed lines between the thirds ([zones](zones.md)) |
| **regions**       | each player's desired region: a ring at its centre joined to him, a dot at its tactical target ([desired region](desired-region.md)) |
| **lines**         | each side's defensive line (solid) and front line (faint), and the defensive line its phase's instruction asks for (dashed) |
| **press**         | a running press: a ring around the pressed carrier, pressers joined to him, each blocked lane dashed from the carrier to the receiver it cuts off with its blocker joined to it, covers dotted to whom they cover ([pressing](pressing.md)) |

Each side's tactic and phase stand above its half of the pitch.

The side panel lists the match (tick, time, state hash, ball owner and last
touch); each side's tactic, phase, defensive line against the one asked for,
length and width, and running press with its trigger and roles; the selected
player's position, speed, target, observations, his latest action decision
(action, subject, utility and the dominant reason) and the candidate table of
his latest pass decision (`*` marks the choice); and the latest
[events](match-events.md), newest first.

| Control            | Keys                         | Effect                                 |
|--------------------|------------------------------|----------------------------------------|
| Play / Pause       | Space                        | play at the chosen speed, or pause    |
| ◀ / ▶              | Left / Right (Shift: ten)    | step one tick back or forward, paused |
| speed              |                              | 0.25x to 8x match time                 |
| slider             |                              | jump to any tick                       |

## Frame format

`sim-cli --frames-out` writes one JSON document. It is read-only output for the
viewer, versioned separately from the [replay format](replay-format.md): nothing
reads it back into a simulation, and a replay, not a frame file, is what
reproduces a match. Positions, velocities and scores are rounded to three
decimals, which keeps a 30-second match of fourteen players around 17 MB.
`--frames-out` records at most 3600 ticks (two minutes at 30 Hz,
`kMaxFrameTicks`): the recording is held in memory until it is written, and a
whole match would run to gigabytes that neither the CLI nor a browser handles
well. The viewer is for looking at situations; a replay records any length. An
abridged example with illustrative values:

```json
{
    "format": "elyverse-debug-frames",
  "version": 2,
  "coreVersion": "0.7.0",
  "scenario": "m0-acceptance",
  "seed": "42",
  "ticksPerSecond": 30,
  "perception": { "viewDistance": 60, "fieldOfViewDegrees": 180, "awarenessRadius": 3 },
  "reception": { "controlRadius": 1 },
  "pitch": { "length": 60, "width": 40 },
  "players": [{ "id": 1, "side": "home" }],
  "frames": [
    {
      "tick": 1,
      "stateHash": "d96934b44b13ded7",
      "ball": { "position": [30, 20], "velocity": [0, 0], "owner": null, "lastTouch": null },
      "pendingPass": null,
      "players": [
        {
          "id": 1,
          "position": [3, 20],
          "velocity": [0, 0],
          "facing": [1, 0],
          "target": null,
          "observations": [
            { "entity": "ball", "position": [30, 20], "velocity": [0, 0], "confidence": 1, "lastSeen": 0 }
          ]
        }
      ],
      "events": [{ "tick": 0, "type": "possessionChanged", "previousOwner": null, "newOwner": 4 }],
      "decisions": [
        {
          "tick": 0,
          "player": 4,
          "outcome": "passed",
          "chosen": 0,
          "candidates": [
            {
              "receiver": 7, "target": [40, 15], "distance": 12.1, "speed": 9.6,
              "receiverConfidence": 1, "interceptionRisk": 0.02, "completion": 0.98,
              "progression": 0.12, "receiverPressure": 0.1, "utility": 1.02, "rejection": "valid"
            }
          ],
          "observations": []
        }
      ]
    }
  ]
}
```

- The header holds what never changes during a match: the run's identity (core
  version, scenario, seed as a decimal string like in a replay), the tick rate,
  the perception and reception parameters the viewer draws, the pitch, and the
  squad with each player's side.
- `frames` has one entry per tick from 0, the initial state, to the last
  simulated tick. A frame is the state after the step that reached its tick,
  plus the events and decision diagnostics that step recorded; frame 0 has
  none. Events and decisions carry their own `tick`, the step's, which is one
  less than the frame's: the step from tick 18 reaches frame 19. The viewer
  shows that tick in the event log and the decision panel.
- Vectors are `[x, y]` arrays in meters, `x` along the pitch length and `y`
  across it. Ids are player ids; `null` means none.
- `observations` of a player are his memory after the step. The observations
  of a decision are the memory the decision was made from, which is the
  previous frame's.
- The header's `zones` hold the pitch `y` of every boundary between lanes and
  the pitch `x` of the boundaries between thirds ([zones](zones.md)).
- Frames carry `teams`: each side's `tactic` name, `phase`, the phase's
  `instruction` (`lineHeight`, `blockLength`, `blockWidth` as fractions of the
  pitch, `pressingIntensity`), its running `press` (`carrier`, `since`,
  `trigger`, `assignments` of `player`, `role` and `subject`) and its `shape`
  (lines as depths from its own goal line, length, width, centroid). Each
  player has his `region` (tactical target, centre, cost) and `action` (type,
  target, subject), and `actions` lists the decisions of players without the
  ball, each candidate with its weighted `scores`, `utility` and `dominant`
  part; see [off-ball movement](off-ball-movement.md),
  [defensive shape](defensive-shape.md) and [pressing](pressing.md). They are
  `null` or empty for a scripted side.
- `pitchControl` is home's control of every cell of the
  [pitch control](pitch-control.md) grid, column by column, rounded to
  hundredths -- away's is the rest -- in the frames whose step refreshed the
  grid, `null` in all others: the viewer shows the latest.
- Event `type`s are the [match events](match-events.md) in lower camel case --
  `passAttempted`, `pressingStarted`, `pitchControlSampled`, ... -- with their
  fields; sides and phases are spelled `home`,
  `buildUp`, ... as in [tactics](tactics.md). The viewer shows an event type it
  does not know by its name. A decision's `outcome` is `passed` or
  `noValidOption`, `chosen` indexes `candidates`, and `rejection` is `valid` or
  the reason a candidate cannot be played.

A change that breaks existing readers raises `version`; the viewer rejects
versions it does not know. Version 2 added `zones`, `pitchControl` and the
teams' `tactic`, `instruction` and `press`.

## Development

`apps/sim-viewer` depends on TypeScript only, pinned to an exact version; as
TypeScript has no dependencies of its own, no lockfile is kept.

| Path              | Contents                                                        |
|-------------------|-----------------------------------------------------------------|
| `src/frames.ts`   | the frame format's types, parsing and lookups                   |
| `src/geometry.ts` | pitch-to-canvas mapping, vision cones, picking a player         |
| `src/playback.ts` | which frame is shown: play, pause, step, seek, speed            |
| `src/render.ts`   | drawing a frame on the canvas                                   |
| `src/overlays.ts` | the tactical overlays                                           |
| `src/panel.ts`    | the side panel                                                  |
| `src/main.ts`     | wiring the page together                                        |
| `test/`           | `node:test` tests of the pure modules, run on the compiled output |

`npm test` type-checks, compiles to `dist/` and runs the tests; CI runs it in
the `viewer` job, and `make viewer-test` runs install and tests from the
repository root.
