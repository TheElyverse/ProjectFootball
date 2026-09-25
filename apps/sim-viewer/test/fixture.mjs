// A minimal, valid frame file: two players, three frames.

export function fixture() {
  const player = (id, x, y) => ({
    id,
    position: [x, y],
    velocity: [0, 0],
    facing: [1, 0],
    target: null,
    observations: [],
  });
  const frame = (tick, extra = {}) => ({
    tick,
    stateHash: "0000000000000000",
    ball: { position: [30, 20], velocity: [0, 0], owner: null, lastTouch: null },
    pendingPass: null,
    players: [player(1, 20, 20), player(2, 40, 20)],
    events: [],
    decisions: [],
    ...extra,
  });
  return {
    format: "elyverse-debug-frames",
    version: 1,
    coreVersion: "0.7.0",
    scenario: "test",
    seed: "42",
    ticksPerSecond: 30,
    perception: { viewDistance: 60, fieldOfViewDegrees: 180, awarenessRadius: 3 },
    reception: { controlRadius: 1 },
    pitch: { length: 60, width: 40 },
    players: [
      { id: 1, side: "home" },
      { id: 2, side: "away" },
    ],
    frames: [
      frame(0),
      frame(1, {
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
