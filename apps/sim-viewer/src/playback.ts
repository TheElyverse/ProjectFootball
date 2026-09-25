// Which frame is on screen. Playback walks through recorded frames at a
// chosen speed; it never runs the simulation, so pausing, stepping or
// changing speed cannot change what happens in the match.

export const SPEEDS: readonly number[] = [0.25, 0.5, 1, 2, 4, 8];

const FRAME_TOLERANCE = 1e-9;

export class Playback {
  private indexValue = 0;
  private playingValue = false;
  private speedValue = 1;
  // Frames earned by elapsed wall-clock time but not shown yet: a fraction
  // below one carries over to the next call.
  private pendingFrames = 0;

  constructor(
    readonly frameCount: number,
    readonly ticksPerSecond: number,
  ) {
    if (frameCount < 1) {
      throw new Error("Playback needs at least one frame");
    }
    if (!(ticksPerSecond > 0)) {
      throw new Error("Playback needs a positive tick rate");
    }
  }

  get index(): number {
    return this.indexValue;
  }

  get playing(): boolean {
    return this.playingValue;
  }

  get speed(): number {
    return this.speedValue;
  }

  get atEnd(): boolean {
    return this.indexValue === this.frameCount - 1;
  }

  // Play from the start again when already at the end.
  play(): void {
    if (this.atEnd) {
      this.indexValue = 0;
    }
    this.playingValue = true;
    this.pendingFrames = 0;
  }

  pause(): void {
    this.playingValue = false;
    this.pendingFrames = 0;
  }

  togglePlaying(): void {
    if (this.playingValue) {
      this.pause();
    } else {
      this.play();
    }
  }

  // Match seconds per wall-clock second; 1 is real time.
  setSpeed(speed: number): void {
    if (!(speed > 0)) {
      throw new Error(`invalid playback speed ${speed}`);
    }
    this.speedValue = speed;
  }

  // Moves by whole frames and pauses: single-tick stepping.
  step(frames: number): void {
    this.pause();
    this.seek(this.indexValue + frames);
  }

  // Jumps to a frame, clamped to the recording.
  seek(index: number): void {
    this.indexValue = Math.min(Math.max(Math.trunc(index), 0), this.frameCount - 1);
  }

  // Advances by the wall-clock time since the last call while playing, and
  // stops at the last frame. Returns whether the frame changed.
  advance(elapsedMilliseconds: number): boolean {
    if (!this.playingValue) {
      return false;
    }
    this.pendingFrames +=
      (Math.max(elapsedMilliseconds, 0) * this.ticksPerSecond * this.speedValue) / 1000;
    // The tolerance keeps rounding from losing a frame, as in 1000 ms at 30 Hz
    // adding up to 29.999... frames.
    const frames = Math.floor(this.pendingFrames + FRAME_TOLERANCE);
    if (frames === 0) {
      return false;
    }
    this.pendingFrames = Math.max(this.pendingFrames - frames, 0);
    const before = this.indexValue;
    this.seek(this.indexValue + frames);
    if (this.atEnd) {
      this.pause();
    }
    return this.indexValue !== before;
  }
}
