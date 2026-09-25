// The debug frame format written by `sim-cli --frames-out` (docs/debug-viewer.md).
// The viewer only reads frames: it never simulates, so nothing it does can
// change a match.

export const FRAMES_FORMAT = "elyverse-debug-frames";
export const FRAMES_VERSION = 2;

// [x, y] in meters; x along the pitch length, y across it.
export type Vec2 = readonly [number, number];

export type TeamSide = "home" | "away";

export interface PlayerInfo {
  readonly id: number;
  readonly side: TeamSide;
}

export interface Observation {
  // "ball", or the id of the observed player.
  readonly entity: "ball" | number;
  readonly position: Vec2;
  readonly velocity: Vec2;
  readonly confidence: number;
  readonly lastSeen: number;
}

// Where the tactic wants a player (docs/desired-region.md).
export interface DesiredRegion {
  readonly tacticalTarget: Vec2;
  readonly center: Vec2;
  readonly cost: number;
}

// A player's decided action without the ball: his assignment.
export interface PlayerAction {
  readonly type: string;
  readonly target: Vec2;
  readonly subject: number | null;
}

export interface PlayerFrame {
  readonly id: number;
  readonly position: Vec2;
  readonly velocity: Vec2;
  readonly facing: Vec2;
  readonly target: Vec2 | null;
  readonly observations: readonly Observation[];
  // Null for a player of a scripted side, and for goalkeepers' actions.
  readonly region: DesiredRegion | null;
  readonly action: PlayerAction | null;
}

// Depths are meters from the side's own goal line (docs/zones.md).
export interface TeamShape {
  readonly defensiveLine: number;
  readonly midfieldLine: number;
  readonly frontLine: number;
  readonly length: number;
  readonly width: number;
  readonly centroid: Vec2;
}

// The current phase's instruction; heights and sizes are fractions of the
// pitch (docs/tactics.md).
export interface PhaseInstruction {
  readonly lineHeight: number;
  readonly blockLength: number;
  readonly blockWidth: number;
  readonly pressingIntensity: number;
}

export interface PressAssignment {
  readonly player: number;
  readonly role: string;
  readonly subject: number;
}

export interface TeamPress {
  readonly carrier: number;
  readonly since: number;
  readonly trigger: string | null;
  readonly assignments: readonly PressAssignment[];
}

export interface TeamFrame {
  readonly side: TeamSide;
  readonly tactic: string | null;
  readonly phase: string | null;
  readonly instruction: PhaseInstruction | null;
  readonly press: TeamPress | null;
  readonly shape: TeamShape | null;
}

// Home's control of each cell, column by column; away's is the rest.
export interface PitchControlFrame {
  readonly columns: number;
  readonly rows: number;
  readonly cellSize: number;
  readonly home: readonly number[];
}

export interface ActionCandidate {
  readonly type: string;
  readonly target: Vec2;
  readonly subject: number | null;
  readonly scores: Readonly<Record<string, number>>;
  readonly utility: number;
  readonly dominant: string;
}

export interface ActionDecision {
  readonly tick: number;
  readonly player: number;
  readonly chosen: number | null;
  readonly assigned: boolean;
  readonly candidates: readonly ActionCandidate[];
}

export interface BallFrame {
  readonly position: Vec2;
  readonly velocity: Vec2;
  readonly owner: number | null;
  readonly lastTouch: number | null;
}

export interface PendingPass {
  readonly passer: number;
  readonly target: Vec2;
  readonly speed: number;
  readonly receiver: number | null;
}

// Every event carries the tick of the step that recorded it, which is one
// before the tick of the frame holding it: the step from tick N reaches N + 1.
export type MatchEvent = { readonly tick: number } & (
  | {
      readonly type: "passAttempted";
      readonly passer: number;
      readonly intendedReceiver: number | null;
      readonly from: Vec2;
      readonly target: Vec2;
      readonly speed: number;
    }
  | { readonly type: "passReceived"; readonly receiver: number; readonly passer: number }
    | {
      readonly type: "passIntercepted";
      readonly interceptor: number;
      readonly passer: number;
      readonly position: Vec2;
    }
  | { readonly type: "looseBallRecovered"; readonly player: number; readonly position: Vec2 }
  | { readonly type: "pitchControlSampled"; readonly homeShare: number; readonly ball: Vec2 }
  | {
      readonly type: "possessionChanged";
      readonly previousOwner: number | null;
      readonly newOwner: number | null;
    }
    | {
      readonly type: "ballWon";
      readonly winner: number;
      readonly loser: number;
      readonly position: Vec2;
    }
    | {
      readonly type: "pressingStarted";
      readonly side: TeamSide;
      readonly carrier: number;
      readonly trigger: string | null;
      readonly assignments: readonly {
        readonly player: number;
        readonly role: string;
        readonly subject: number;
      }[];
    }
    | { readonly type: "pressingEnded"; readonly side: TeamSide; readonly outcome: string }
  | {
      readonly type: "tacticChanged";
      readonly side: TeamSide;
      readonly tactic: string;
      readonly contentHash: string;
    }
  | {
      readonly type: "phaseChanged";
      readonly side: TeamSide;
      readonly previous: string | null;
      readonly phase: string;
    }
);

export interface PassCandidate {
  readonly receiver: number;
  readonly target: Vec2;
  readonly distance: number;
  readonly speed: number;
  readonly receiverConfidence: number;
  readonly interceptionRisk: number;
  readonly completion: number;
  readonly progression: number;
  readonly receiverPressure: number;
  readonly utility: number;
  // "valid", or why the pass cannot be played.
  readonly rejection: string;
}

export interface Decision {
  // The tick decided on: the step's, one before the frame's.
  readonly tick: number;
  readonly player: number;
  readonly outcome: "passed" | "noValidOption";
  // Index into candidates of the chosen pass.
  readonly chosen: number | null;
  readonly candidates: readonly PassCandidate[];
  readonly observations: readonly Observation[];
}

export interface Frame {
  readonly tick: number;
  readonly stateHash: string;
  readonly ball: BallFrame;
  readonly pendingPass: PendingPass | null;
  readonly players: readonly PlayerFrame[];
    readonly events: readonly MatchEvent[];
  readonly decisions: readonly Decision[];
  readonly actions: readonly ActionDecision[];
  readonly teams: readonly TeamFrame[];
  // Only in frames whose step refreshed the grid; see latestPitchControl().
  readonly pitchControl: PitchControlFrame | null;
}

export interface Recording {
  readonly format: string;
  readonly version: number;
  readonly coreVersion: string;
  readonly scenario: string;
  readonly seed: string;
  readonly ticksPerSecond: number;
  readonly perception: {
    readonly viewDistance: number;
    readonly fieldOfViewDegrees: number;
    readonly awarenessRadius: number;
  };
  readonly reception: { readonly controlRadius: number };
    readonly pitch: { readonly length: number; readonly width: number };
  // Pitch y between lanes and pitch x between thirds.
  readonly zones: {
    readonly laneBoundaries: readonly number[];
    readonly thirdBoundaries: readonly number[];
  };
  readonly players: readonly PlayerInfo[];
  readonly frames: readonly Frame[];
}

function isObject(value: unknown): value is Record<string, unknown> {
  return typeof value === "object" && value !== null && !Array.isArray(value);
}

// Parses a frame file. Checks the header and the fields the viewer relies on
// everywhere; the rest of each frame is trusted to match the format, since
// the file comes from the CLI, not from a user.
export function parseRecording(text: string): Recording {
  let json: unknown;
  try {
    json = JSON.parse(text);
  } catch (error) {
    throw new Error(`not a JSON document: ${(error as Error).message}`);
  }
  if (!isObject(json) || json["format"] !== FRAMES_FORMAT) {
    throw new Error(`not a debug frame file (expected format "${FRAMES_FORMAT}")`);
  }
  if (json["version"] !== FRAMES_VERSION) {
    throw new Error(
      `unsupported frame format version ${String(json["version"])}, ` +
        `this viewer reads version ${FRAMES_VERSION}`,
    );
  }
  const pitch = json["pitch"];
  if (
    !isObject(pitch) ||
    typeof pitch["length"] !== "number" ||
    typeof pitch["width"] !== "number"
  ) {
    throw new Error("pitch: expected length and width");
  }
  if (typeof json["ticksPerSecond"] !== "number" || json["ticksPerSecond"] <= 0) {
    throw new Error("ticksPerSecond: expected a positive number");
  }
  const frames = json["frames"];
  if (!Array.isArray(frames) || frames.length === 0) {
    throw new Error("frames: expected at least one frame");
  }
  if (!Array.isArray(json["players"])) {
    throw new Error("players: expected an array");
  }
  return json as unknown as Recording;
}

// Which side a player is on, from the squad list.
export function sideOf(recording: Recording, playerId: number): TeamSide | undefined {
  return recording.players.find((player) => player.id === playerId)?.side;
}

// Match time of a frame in seconds.
export function frameSeconds(recording: Recording, frame: Frame): number {
  return frame.tick / recording.ticksPerSecond;
}

// The latest decision of a player at or before frameIndex, with the index of
// its frame; undefined if he has not decided yet. Decisions are rare, so a
// backward scan is fast enough for every redraw.
export function latestDecision(
  recording: Recording,
  frameIndex: number,
  playerId: number,
): { readonly decision: Decision; readonly frameIndex: number } | undefined {
  for (let index = Math.min(frameIndex, recording.frames.length - 1); index >= 0; index -= 1) {
    const decision = recording.frames[index]?.decisions.find((entry) => entry.player === playerId);
    if (decision !== undefined) {
      return { decision, frameIndex: index };
    }
  }
  return undefined;
}

// The latest action decision of a player at or before frameIndex, like
// latestDecision() for players without the ball.
export function latestActionDecision(
  recording: Recording,
  frameIndex: number,
  playerId: number,
): { readonly decision: ActionDecision; readonly frameIndex: number } | undefined {
  for (let index = Math.min(frameIndex, recording.frames.length - 1); index >= 0; index -= 1) {
    const decision = recording.frames[index]?.actions.find((entry) => entry.player === playerId);
    if (decision !== undefined) {
      return { decision, frameIndex: index };
    }
  }
  return undefined;
}

// The pitch control grid in force at frameIndex: the latest one recorded at
// or before it.
export function latestPitchControl(
  recording: Recording,
  frameIndex: number,
): PitchControlFrame | undefined {
  for (let index = Math.min(frameIndex, recording.frames.length - 1); index >= 0; index -= 1) {
    const grid = recording.frames[index]?.pitchControl;
    if (grid !== undefined && grid !== null) {
      return grid;
    }
  }
  return undefined;
}

// The pitch x of a depth measured from a side's own goal line.
export function depthToX(recording: Recording, side: TeamSide, depth: number): number {
  return side === "home" ? depth : recording.pitch.length - depth;
}

// Whether the event log lists an event: the pitch control system's regular
// samples would drown out everything else.
export function isLoggedEvent(event: MatchEvent): boolean {
  return event.type !== "pitchControlSampled";
}

// A one-line description of an event for the event log.
export function describeEvent(event: MatchEvent): string {
  switch (event.type) {
    case "passAttempted":
      return event.intendedReceiver === null
        ? `#${event.passer} passes into space`
        : `#${event.passer} passes to #${event.intendedReceiver}`;
    case "passReceived":
      return `#${event.receiver} receives from #${event.passer}`;
    case "passIntercepted":
      return `#${event.interceptor} intercepts #${event.passer}'s pass`;
    case "looseBallRecovered":
      return `#${event.player} recovers the loose ball`;
    case "possessionChanged":
      return event.newOwner === null
        ? `ball free (was #${event.previousOwner ?? "-"})`
        : `#${event.newOwner} has the ball`;
        case "phaseChanged":
      return `${event.side}: ${event.phase}`;
        case "ballWon":
      return `#${event.winner} wins the ball from #${event.loser}`;
    case "pressingStarted":
      return `${event.side} presses #${event.carrier} (${event.trigger ?? "pressing phase"}, ${event.assignments.length} players)`;
    case "pressingEnded":
            return `${event.side} press ends: ${event.outcome}`;
        case "tacticChanged":
      return `${event.side} switches to ${event.tactic}`;
    case "pitchControlSampled":
      return `home controls ${Math.round(event.homeShare * 100)}% of the pitch`;
    default:
      // A newer core may record events this viewer does not know yet.
      return (event as { readonly type: string }).type;
  }
}
