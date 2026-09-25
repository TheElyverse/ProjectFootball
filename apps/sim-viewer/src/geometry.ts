import type { PlayerFrame, Vec2 } from "./frames.js";

// Maps pitch meters to canvas pixels: the whole pitch plus a margin, as large
// as the canvas allows, centered. Canvas y grows downward like pitch y, so
// y = 0 is the top touchline.
export class Viewport {
  readonly scale: number;
  readonly offsetX: number;
  readonly offsetY: number;

  constructor(
    canvasWidth: number,
    canvasHeight: number,
    pitchLength: number,
    pitchWidth: number,
    marginMeters = 3,
  ) {
    const totalLength = pitchLength + 2 * marginMeters;
    const totalWidth = pitchWidth + 2 * marginMeters;
    this.scale = Math.max(Math.min(canvasWidth / totalLength, canvasHeight / totalWidth), 1e-6);
    this.offsetX = (canvasWidth - pitchLength * this.scale) / 2;
    this.offsetY = (canvasHeight - pitchWidth * this.scale) / 2;
  }

  toCanvas(point: Vec2): Vec2 {
    return [this.offsetX + point[0] * this.scale, this.offsetY + point[1] * this.scale];
  }

  toPitch(point: Vec2): Vec2 {
    return [(point[0] - this.offsetX) / this.scale, (point[1] - this.offsetY) / this.scale];
  }

  // A length in meters as pixels.
  pixels(meters: number): number {
    return meters * this.scale;
  }
}

export function distance(from: Vec2, to: Vec2): number {
  return Math.hypot(to[0] - from[0], to[1] - from[1]);
}

// The outline of a vision cone in pitch meters: the apex, then points along
// the arc from one edge of the field of view to the other. A field of view of
// 360 degrees or more is a full circle around the apex.
export function visionCone(
  apex: Vec2,
  facing: Vec2,
  viewDistance: number,
  fieldOfViewDegrees: number,
  arcSegments = 24,
): Vec2[] {
  const heading = Math.atan2(facing[1], facing[0]);
  const halfAngle = (Math.min(fieldOfViewDegrees, 360) * Math.PI) / 360;
  const points: Vec2[] = fieldOfViewDegrees >= 360 ? [] : [apex];
  for (let segment = 0; segment <= arcSegments; segment += 1) {
    const angle = heading - halfAngle + (2 * halfAngle * segment) / arcSegments;
    points.push([
      apex[0] + Math.cos(angle) * viewDistance,
      apex[1] + Math.sin(angle) * viewDistance,
    ]);
  }
  return points;
}

// The player closest to point within maxDistance meters, for selecting a
// player by clicking; undefined if nobody is that close.
export function playerAt(
  players: readonly PlayerFrame[],
  point: Vec2,
  maxDistance: number,
): PlayerFrame | undefined {
  let closest: PlayerFrame | undefined;
  let closestDistance = maxDistance;
  for (const player of players) {
    const playerDistance = distance(player.position, point);
    if (playerDistance <= closestDistance) {
      closest = player;
      closestDistance = playerDistance;
    }
  }
  return closest;
}
