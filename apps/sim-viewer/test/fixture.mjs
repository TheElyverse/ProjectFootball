// A minimal, valid frame file: two players, three frames.

export function fixture() {
  const player = (id, x, y) => ({
    id,
    position: [x, y],
    velocity: [0, 0],
    facing: [1, 0],
        target: null,
    observations: [],
    region: null,
    action: null,
  });
  const frame = (tick, extra = {}) => ({
    tick,
    stateHash: "0000000000000000",
    ball: { position: [30, 20], velocity: [0, 0], owner: null, lastTouch: null },
    pendingPass: null,
    players: [player(1, 20, 20), player(2, 40, 20)],
        events: [],
    decisions: [],
    actions: [],
    teams: [
      { side: "home", tactic: null, phase: null, instruction: null, press: null, shape: null },
      { side: "away", tactic: null, phase: null, instruction: null, press: null, shape: null },
    ],
    pitchControl: null,
    ...extra,
  });
  return {
    format: "elyverse-debug-frames",
        version: 2,
    coreVersion: "0.7.0",
    scenario: "test",
    seed: "42",
    ticksPerSecond: 30,
    perception: { viewDistance: 60, fieldOfViewDegrees: 180, awarenessRadius: 3 },
    reception: { controlRadius: 1 },
        pitch: { length: 60, width: 40 },
    zones: { laneBoundaries: [8, 16, 24, 32], thirdBoundaries: [20, 40] },
    players: [
      { id: 1, side: "home" },
      { id: 2, side: "away" },
    ],
    frames: [
      frame(0),
            frame(1, {
        pitchControl: { columns: 2, rows: 1, cellSize: 30, home: [0.8, 0.3] },
        actions: [
          {
            tick: 0,
            player: 2,
            chosen: 0,
            assigned: false,
            candidates: [
              {
                type: "holdPosition",
                target: [40, 20],
                subject: null,
                scores: { responsibility: 0.4 },
                utility: 0.4,
                dominant: "responsibility",
              },
            ],
          },
        ],
        decisions: [
          {
            tick: 0,
            player: 1,
            outcome: "noValidOption",
            chosen: null,
            candidates: [],
            observations: [],
          },
        ],
      }),
      frame(2),
    ],
  };
}
