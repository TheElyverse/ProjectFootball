// A minimal, valid frame file: two players, three frames.

export function fixture() {
  const player = (id, x, y) => ({
    id,
    position: [x, y],
    velocity: [0, 0],
    facing: [1, 0],
    look: [1, 0],
        target: null,
    observations: [],
    region: null,
    action: null,
  });
  const frame = (tick, extra = {}) => ({
    tick,
    stateHash: "0000000000000000",
    clock: "00:00",
    period: "firstHalf",
    endsChanged: false,
    ball: {
      position: [30, 20],
      velocity: [0, 0],
      height: 0,
      verticalVelocity: 0,
      spin: 0,
      owner: null,
      lastTouch: null,
    },
    pendingPass: null,
    score: { home: 0, away: 0 },
    setPiece: null,
    keepers: [],
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
        version: 6,
    coreVersion: "0.7.0",
    scenario: "test",
    seed: "42",
    ticksPerSecond: 30,
    perception: { viewDistance: 60, fieldOfViewDegrees: 180, awarenessRadius: 3 },
    reception: { controlRadius: 1 },
        pitch: { length: 60, width: 40 },
    zones: { laneBoundaries: [8, 16, 24, 32], thirdBoundaries: [20, 40] },
    geometry: {
      ends: [
        {
          goalLineX: 0,
          posts: [
            [0, 16.34],
            [0, 23.66],
          ],
          crossbar: 2.44,
          penaltyArea: { min: [0, 5.84], max: [16.5, 34.16] },
          goalArea: { min: [0, 14.84], max: [5.5, 25.16] },
          penaltySpot: [11, 20],
        },
        {
          goalLineX: 60,
          posts: [
            [60, 16.34],
            [60, 23.66],
          ],
          crossbar: 2.44,
          penaltyArea: { min: [43.5, 5.84], max: [60, 34.16] },
          goalArea: { min: [54.5, 14.84], max: [60, 25.16] },
          penaltySpot: [49, 20],
        },
      ],
      centerCircleRadius: 9.15,
    },
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
            idlePress: false,
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
            reason: "scanFirstSide",
            target: [10, 20],
            candidates: [],
            shots: [
              {
                target: [60, 18],
                height: 1.2,
                type: "driven",
                goalChance: 0.1,
                utility: -0.2,
                rejection: "notWorthIt",
              },
            ],
            chosenShot: null,
            observations: [],
          },
        ],
      }),
      frame(2, {
        events: [{ tick: 2, type: "setPieceTaken", side: "home", kind: "corner", taker: 1 }],
        setPiece: {
          kind: "corner",
          side: "home",
          taker: 1,
          spot: [60, 0],
          indirect: false,
          places: [
            { player: 1, target: [59.5, 0.5] },
            { player: 2, target: [55, 20] },
          ],
        },
        keepers: [{ player: 2, onLine: [60, 19], target: [58.5, 19.5] }],
      }),
    ],
  };
}
