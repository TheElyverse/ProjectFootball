import {
  depthToX,
  latestActionDecision,
  latestPitchControl,
  type Frame,
  type Recording,
  type TeamFrame,
  type TeamSide,
  type Vec2,
} from "./frames.js";
import type { Viewport } from "./geometry.js";

// The tactical layers the viewer can draw over a frame (docs/debug-viewer.md).
export interface Overlays {
  readonly pitchControl: boolean;
  readonly zones: boolean;
  readonly regions: boolean;
  readonly shape: boolean;
  readonly press: boolean;
}

export const NO_OVERLAYS: Overlays = {
  pitchControl: false,
  zones: false,
  regions: false,
  shape: false,
  press: false,
};

const TEAM_RGB: Readonly<Record<TeamSide, string>> = {
  home: "79, 142, 247",
  away: "229, 83, 75",
};

// The fill of a pitch control cell: the controlling side's color, the more
// opaque the clearer its control; transparent where both are equal.
export function controlColor(homeControl: number): string {
  const side: TeamSide = homeControl >= 0.5 ? "home" : "away";
  const alpha = Math.min(Math.abs(homeControl - 0.5) * 1.2, 0.6);
  return `rgba(${TEAM_RGB[side]}, ${alpha.toFixed(3)})`;
}

function teamColor(side: TeamSide, alpha = 1): string {
  return `rgba(${TEAM_RGB[side]}, ${alpha})`;
}

function segment(context: CanvasRenderingContext2D, viewport: Viewport, from: Vec2, to: Vec2): void {
  const [fromX, fromY] = viewport.toCanvas(from);
  const [toX, toY] = viewport.toCanvas(to);
  context.beginPath();
  context.moveTo(fromX, fromY);
  context.lineTo(toX, toY);
  context.stroke();
}

function ring(
  context: CanvasRenderingContext2D,
  viewport: Viewport,
  center: Vec2,
  radiusPixels: number,
): void {
  const [x, y] = viewport.toCanvas(center);
  context.beginPath();
  context.arc(x, y, radiusPixels, 0, 2 * Math.PI);
}

function drawPitchControl(
  context: CanvasRenderingContext2D,
  viewport: Viewport,
  recording: Recording,
  frameIndex: number,
): void {
  const grid = latestPitchControl(recording, frameIndex);
  if (grid === undefined) {
    return;
  }
  const { length, width } = recording.pitch;
  for (let column = 0; column < grid.columns; column += 1) {
    for (let row = 0; row < grid.rows; row += 1) {
      const control = grid.home[column * grid.rows + row];
      if (control === undefined) {
        continue;
      }
      // The last column and row may reach past the pitch: clip to it.
      const x = column * grid.cellSize;
      const y = row * grid.cellSize;
      const [left, top] = viewport.toCanvas([x, y]);
      context.fillStyle = controlColor(control);
      context.fillRect(
        left,
        top,
        viewport.pixels(Math.min(grid.cellSize, length - x)),
        viewport.pixels(Math.min(grid.cellSize, width - y)),
      );
    }
  }
}

function drawZones(
  context: CanvasRenderingContext2D,
  viewport: Viewport,
  recording: Recording,
): void {
  const { length, width } = recording.pitch;
  context.strokeStyle = "rgba(255, 255, 255, 0.35)";
  context.lineWidth = 1;
  context.setLineDash([2, 6]);
  for (const y of recording.zones.laneBoundaries) {
    segment(context, viewport, [0, y], [length, y]);
  }
  context.setLineDash([8, 6]);
  for (const x of recording.zones.thirdBoundaries) {
    segment(context, viewport, [x, 0], [x, width]);
  }
  context.setLineDash([]);
}

// Every player's desired region: a ring at its centre joined to him, and a
// dot at the tactical target it was derived from.
function drawRegions(
  context: CanvasRenderingContext2D,
  viewport: Viewport,
  recording: Recording,
  frame: Frame,
): void {
  for (const player of frame.players) {
    if (player.region === null) {
      continue;
    }
    const side = recording.players.find((entry) => entry.id === player.id)?.side ?? "home";
    context.strokeStyle = teamColor(side, 0.8);
    context.fillStyle = teamColor(side, 0.8);
    context.lineWidth = 1;
    context.setLineDash([2, 3]);
    segment(context, viewport, player.position, player.region.center);
    context.setLineDash([]);
    ring(context, viewport, player.region.center, Math.max(viewport.pixels(1.5), 5));
    context.stroke();
    ring(context, viewport, player.region.tacticalTarget, 2);
    context.fill();
  }
}

// A team's lines across the pitch: the defensive and front line it holds,
// solid, and the defensive line its instruction asks for, dashed.
function drawShape(
  context: CanvasRenderingContext2D,
  viewport: Viewport,
  recording: Recording,
  team: TeamFrame,
): void {
  const { length, width } = recording.pitch;
  const vertical = (depth: number): void => {
    const x = depthToX(recording, team.side, depth);
    segment(context, viewport, [x, 0], [x, width]);
  };
  context.lineWidth = 2;
  if (team.shape !== null) {
    context.strokeStyle = teamColor(team.side, 0.7);
    vertical(team.shape.defensiveLine);
    context.strokeStyle = teamColor(team.side, 0.35);
    vertical(team.shape.frontLine);
  }
  if (team.instruction !== null) {
    context.strokeStyle = teamColor(team.side, 0.9);
    context.setLineDash([10, 6]);
    vertical(team.instruction.lineHeight * length);
    context.setLineDash([]);
  }
}

// A team's press: pressers joined to the carrier, the lanes the blockers
// close from the carrier to their subjects, the cover behind the presser.
function drawPress(
  context: CanvasRenderingContext2D,
  viewport: Viewport,
  frame: Frame,
  team: TeamFrame,
): void {
  const press = team.press;
  if (press === null) {
    return;
  }
  const positionOf = (id: number): Vec2 | undefined =>
    frame.players.find((player) => player.id === id)?.position;
  const carrier = positionOf(press.carrier);
  if (carrier === undefined) {
    return;
  }
  context.strokeStyle = teamColor(team.side, 1);
  context.lineWidth = 2;
  ring(context, viewport, carrier, Math.max(viewport.pixels(2.5), 10));
  context.stroke();
  for (const assignment of press.assignments) {
    const player = positionOf(assignment.player);
    const subject = positionOf(assignment.subject);
    if (player === undefined || subject === undefined) {
      continue;
    }
    if (assignment.role === "blockLane") {
      // The blocked lane, carrier to receiver, crossed out by the blocker.
      context.setLineDash([6, 4]);
      segment(context, viewport, carrier, subject);
      context.setLineDash([]);
      segment(context, viewport, player, [(carrier[0] + subject[0]) / 2, (carrier[1] + subject[1]) / 2]);
    } else if (assignment.role === "cover") {
      context.setLineDash([2, 4]);
      segment(context, viewport, player, subject);
      context.setLineDash([]);
    } else {
      segment(context, viewport, player, subject);
    }
  }
}

// Each side's tactic and phase in its half's top corner.
function drawPhases(
  context: CanvasRenderingContext2D,
  viewport: Viewport,
  recording: Recording,
  frame: Frame,
): void {
  context.font = "bold 13px sans-serif";
  context.textBaseline = "bottom";
  for (const team of frame.teams) {
    if (team.phase === null) {
      continue;
    }
    const home = team.side === "home";
    const [x, y] = viewport.toCanvas([home ? 0 : recording.pitch.length, 0]);
    context.textAlign = home ? "left" : "right";
    context.fillStyle = teamColor(team.side, 1);
    context.fillText(`${team.tactic ?? "scripted"}: ${team.phase}`, x, y - 4);
  }
}

// The selected player's latest decision without the ball: every candidate's
// target from where he decided, the chosen one highlighted.
export function drawActionCandidates(
  context: CanvasRenderingContext2D,
  viewport: Viewport,
  recording: Recording,
  frameIndex: number,
  playerId: number,
): void {
  const latest = latestActionDecision(recording, frameIndex, playerId);
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
    context.strokeStyle = chosen ? "#ffd33d" : "rgba(255, 255, 255, 0.55)";
    context.lineWidth = chosen ? 2.5 : 1;
    context.setLineDash(chosen ? [] : [3, 4]);
    segment(context, viewport, origin.position, candidate.target);
    context.setLineDash([]);
    ring(context, viewport, candidate.target, chosen ? 5 : 3);
    context.stroke();
  });
}

// The overlays drawn under the players.
export function drawOverlays(
  context: CanvasRenderingContext2D,
  viewport: Viewport,
  recording: Recording,
  frameIndex: number,
  overlays: Overlays,
): void {
  const frame = recording.frames[frameIndex];
  if (frame === undefined) {
    return;
  }
  if (overlays.pitchControl) {
    drawPitchControl(context, viewport, recording, frameIndex);
  }
  if (overlays.zones) {
    drawZones(context, viewport, recording);
  }
  if (overlays.shape) {
    for (const team of frame.teams) {
      drawShape(context, viewport, recording, team);
    }
  }
  if (overlays.regions) {
    drawRegions(context, viewport, recording, frame);
  }
  if (overlays.press) {
    for (const team of frame.teams) {
      drawPress(context, viewport, frame, team);
    }
  }
  drawPhases(context, viewport, recording, frame);
}
