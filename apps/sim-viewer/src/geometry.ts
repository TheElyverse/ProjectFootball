import type { PlayerFrame, Vec2 } from "./frames.js";

export interface CanvasRect {
  readonly left: number;
  readonly top: number;
  readonly width: number;
  readonly height: number;
}

// Maps pitch meters to canvas pixels: the whole pitch plus a margin, as large
// as the canvas allows, centered. Canvas y grows downward like pitch y, so
// y = 0 is the top touchline -- unless mirrored, as once the teams have
// changed ends: then the pitch is turned round, x = 0 on the right and y = 0
// at the bottom.
export class Viewport {
  readonly scale: number;
  readonly offsetX: number;
  readonly offsetY: number;
  mirrored = false;
  private readonly pitchLength: number;
  private readonly pitchWidth: number;

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
    this.pitchLength = pitchLength;
    this.pitchWidth = pitchWidth;
  }

  toCanvas(point: Vec2): Vec2 {
    const [x, y] = this.turned(point);
    return [this.offsetX + x * this.scale, this.offsetY + y * this.scale];
  }

  toPitch(point: Vec2): Vec2 {
    return this.turned([
      (point[0] - this.offsetX) / this.scale,
      (point[1] - this.offsetY) / this.scale,
    ]);
  }

  // The canvas rectangle of a pitch rectangle from min to max, whichever
  // corners the view puts top left.
  rectToCanvas(min: Vec2, max: Vec2): CanvasRect {
    const [x1, y1] = this.toCanvas(min);
    const [x2, y2] = this.toCanvas(max);
    return {
      left: Math.min(x1, x2),
      top: Math.min(y1, y2),
      width: Math.abs(x2 - x1),
      height: Math.abs(y2 - y1),
    };
  }

  // Half a turn round the centre spot when mirrored; its own inverse.
  private turned(point: Vec2): Vec2 {
    return this.mirrored
      ? [this.pitchLength - point[0], this.pitchWidth - point[1]]
      : [point[0], point[1]];
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
