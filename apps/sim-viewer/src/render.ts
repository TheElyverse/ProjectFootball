import {
  latestDecision,
  sideOf,
  type Frame,
  type PlayerFrame,
  type Recording,
  type Vec2,
} from "./frames.js";
import { visionCone, type Viewport } from "./geometry.js";

// What to draw beyond the plain frame.
export interface RenderOptions {
  readonly frameIndex: number;
  readonly selectedPlayer: number | null;
  readonly showAllTargets: boolean;
}

const COLORS = {
  grass: "#2e7d32",
  surround: "#1b5e20",
  lines: "rgba(255, 255, 255, 0.85)",
  home: "#4f8ef7",
  away: "#e5534b",
  ball: "#ffffff",
  owner: "#ffd33d",
  selection: "#ffffff",
  target: "rgba(255, 255, 255, 0.7)",
  vision: "rgba(255, 255, 200, 0.12)",
  awareness: "rgba(255, 255, 200, 0.35)",
  observation: "#ffe082",
  validPass: "#76ff03",
  chosenPass: "#ffd33d",
  rejectedPass: "rgba(200, 200, 200, 0.6)",
  pendingPass: "#ffd33d",
} as const;

const PLAYER_RADIUS_METERS = 0.8;
const BALL_RADIUS_METERS = 0.35;

function teamColor(recording: Recording, playerId: number): string {
  return sideOf(recording, playerId) === "away" ? COLORS.away : COLORS.home;
}

function line(context: CanvasRenderingContext2D, viewport: Viewport, from: Vec2, to: Vec2): void {
  const [fromX, fromY] = viewport.toCanvas(from);
  const [toX, toY] = viewport.toCanvas(to);
  context.beginPath();
  context.moveTo(fromX, fromY);
  context.lineTo(toX, toY);
  context.stroke();
}

function circle(
  context: CanvasRenderingContext2D,
  viewport: Viewport,
  center: Vec2,
  radiusPixels: number,
): void {
  const [x, y] = viewport.toCanvas(center);
  context.beginPath();
  context.arc(x, y, radiusPixels, 0, 2 * Math.PI);
}

function drawPitch(
  context: CanvasRenderingContext2D,
  viewport: Viewport,
  recording: Recording,
): void {
  const { length, width } = recording.pitch;
  context.fillStyle = COLORS.surround;
  context.fillRect(0, 0, context.canvas.width, context.canvas.height);
  const [left, top] = viewport.toCanvas([0, 0]);
  context.fillStyle = COLORS.grass;
  context.fillRect(left, top, viewport.pixels(length), viewport.pixels(width));

  context.strokeStyle = COLORS.lines;
  context.lineWidth = 2;
  context.setLineDash([]);
  context.strokeRect(left, top, viewport.pixels(length), viewport.pixels(width));
  line(context, viewport, [length / 2, 0], [length / 2, width]);
  // Decoration only: the simulation has no center circle rule yet.
  circle(context, viewport, [length / 2, width / 2], viewport.pixels(Math.min(width, length) / 8));
  context.stroke();
}

function drawTarget(
  context: CanvasRenderingContext2D,
  viewport: Viewport,
  player: PlayerFrame,
): void {
  if (player.target === null) {
    return;
  }
  context.strokeStyle = COLORS.target;
  context.lineWidth = 1;
  context.setLineDash([4, 4]);
  line(context, viewport, player.position, player.target);
  context.setLineDash([]);
  const [x, y] = viewport.toCanvas(player.target);
  const size = 4;
  context.beginPath();
  context.moveTo(x - size, y - size);
  context.lineTo(x + size, y + size);
  context.moveTo(x + size, y - size);
  context.lineTo(x - size, y + size);
  context.stroke();
}

// The selected player's view: vision cone, awareness radius, and what he
// remembers, faded by confidence.
function drawPerception(
  context: CanvasRenderingContext2D,
  viewport: Viewport,
  recording: Recording,
  player: PlayerFrame,
): void {
  const { viewDistance, fieldOfViewDegrees, awarenessRadius } = recording.perception;
  const cone = visionCone(player.position, player.facing, viewDistance, fieldOfViewDegrees);
  context.fillStyle = COLORS.vision;
  context.beginPath();
  cone.forEach((point, index) => {
    const [x, y] = viewport.toCanvas(point);
    if (index === 0) {
      context.moveTo(x, y);
    } else {
      context.lineTo(x, y);
    }
  });
  context.closePath();
  context.fill();

  context.strokeStyle = COLORS.awareness;
  context.lineWidth = 1;
  circle(context, viewport, player.position, viewport.pixels(awarenessRadius));
  context.stroke();

  context.strokeStyle = COLORS.observation;
  context.fillStyle = COLORS.observation;
  for (const observation of player.observations) {
    context.globalAlpha = 0.25 + 0.75 * observation.confidence;
    const radius =
      observation.entity === "ball"
        ? viewport.pixels(BALL_RADIUS_METERS) + 2
        : viewport.pixels(PLAYER_RADIUS_METERS) + 3;
    circle(context, viewport, observation.position, radius);
    context.setLineDash([2, 2]);
    context.stroke();
    context.setLineDash([]);
  }
  context.globalAlpha = 1;
}

// The selected player's latest pass decision: every candidate from the
// position he decided at, the chosen one highlighted.
function drawDecision(
  context: CanvasRenderingContext2D,
  viewport: Viewport,
  recording: Recording,
  options: RenderOptions,
  playerId: number,
): void {
  const latest = latestDecision(recording, options.frameIndex, playerId);
  if (latest === undefined) {
    return;
  }
  const origin = recording.frames[latest.frameIndex]?.players.find(
    (entry) => entry.id === playerId,
  );
  if (origin === undefined) {
    return;
  }
  latest.decision.candidates.forEach((candidate, index) => {
    const chosen = latest.decision.chosen === index;
    const valid = candidate.rejection === "valid";
    context.strokeStyle = chosen
      ? COLORS.chosenPass
      : valid
        ? COLORS.validPass
        : COLORS.rejectedPass;
    context.lineWidth = chosen ? 3 : 1.5;
    context.setLineDash(valid ? [] : [3, 5]);
    line(context, viewport, origin.position, candidate.target);
  });
  context.setLineDash([]);
}

function drawPlayer(
  context: CanvasRenderingContext2D,
  viewport: Viewport,
  recording: Recording,
  frame: Frame,
  player: PlayerFrame,
  selected: boolean,
): void {
  const radius = Math.max(viewport.pixels(PLAYER_RADIUS_METERS), 6);
  const [x, y] = viewport.toCanvas(player.position);
  context.fillStyle = teamColor(recording, player.id);
  circle(context, viewport, player.position, radius);
  context.fill();

  if (frame.ball.owner === player.id) {
    context.strokeStyle = COLORS.owner;
    context.lineWidth = 3;
    circle(context, viewport, player.position, radius + 3);
    context.stroke();
  }
  if (selected) {
    context.strokeStyle = COLORS.selection;
    context.lineWidth = 2;
    circle(context, viewport, player.position, radius + 6);
    context.stroke();
  }

  // Facing: a short line from the center to the edge and beyond.
  context.strokeStyle = COLORS.lines;
  context.lineWidth = 2;
  context.beginPath();
  context.moveTo(x, y);
  context.lineTo(x + player.facing[0] * radius * 1.6, y + player.facing[1] * radius * 1.6);
  context.stroke();

  context.fillStyle = "#ffffff";
  context.font = `bold ${Math.round(radius)}px sans-serif`;
  context.textAlign = "center";
  context.textBaseline = "middle";
  context.fillText(String(player.id), x, y);
}

function drawBall(context: CanvasRenderingContext2D, viewport: Viewport, frame: Frame): void {
  if (frame.pendingPass !== null) {
    const passer = frame.players.find((player) => player.id === frame.pendingPass?.passer);
    if (passer !== undefined) {
      context.strokeStyle = COLORS.pendingPass;
      context.lineWidth = 2;
      context.setLineDash([6, 4]);
      line(context, viewport, passer.position, frame.pendingPass.target);
      context.setLineDash([]);
    }
  }
  context.fillStyle = COLORS.ball;
  context.strokeStyle = "#000000";
  context.lineWidth = 1;
  circle(context, viewport, frame.ball.position, Math.max(viewport.pixels(BALL_RADIUS_METERS), 4));
  context.fill();
  context.stroke();
}

// Draws one frame. Reads the recording only.
export function renderFrame(
  context: CanvasRenderingContext2D,
  viewport: Viewport,
  recording: Recording,
  options: RenderOptions,
): void {
  const frame = recording.frames[options.frameIndex];
  if (frame === undefined) {
    return;
  }
  drawPitch(context, viewport, recording);

  const selected = frame.players.find((player) => player.id === options.selectedPlayer);
  if (selected !== undefined) {
    drawPerception(context, viewport, recording, selected);
    drawDecision(context, viewport, recording, options, selected.id);
  }
  for (const player of frame.players) {
    if (options.showAllTargets || player.id === options.selectedPlayer) {
      drawTarget(context, viewport, player);
    }
  }
  for (const player of frame.players) {
    drawPlayer(context, viewport, recording, frame, player, player.id === options.selectedPlayer);
  }
  drawBall(context, viewport, frame);
}
