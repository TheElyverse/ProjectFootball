import { parseRecording, type Recording } from "./frames.js";
import { playerAt, Viewport } from "./geometry.js";
import { renderPanel } from "./panel.js";
import { Playback, SPEEDS } from "./playback.js";
import { renderFrame } from "./render.js";

// Wires the page: loads a frame file, drives playback, and redraws. All state
// here is the viewer's own; the recording is never modified.

// How far from a player's center a click still selects him, in meters.
const SELECT_DISTANCE_METERS = 2;

function required<T extends HTMLElement>(id: string, type: { new (): T }): T {
  const node = document.getElementById(id);
  if (!(node instanceof type)) {
    throw new Error(`missing element #${id}`);
  }
  return node;
}

const canvas = required("pitch", HTMLCanvasElement);
const panel = required("panel", HTMLElement);
const status = required("status", HTMLElement);
const fileInput = required("file", HTMLInputElement);
const playButton = required("play", HTMLButtonElement);
const backButton = required("back", HTMLButtonElement);
const forwardButton = required("forward", HTMLButtonElement);
const speedSelect = required("speed", HTMLSelectElement);
const slider = required("scrub", HTMLInputElement);
const tickLabel = required("tick", HTMLElement);
const allTargets = required("all-targets", HTMLInputElement);
// One checkbox per overlay, in the order of the Overlays fields.
const overlayBoxes = {
  pitchControl: required("overlay-pitch-control", HTMLInputElement),
  zones: required("overlay-zones", HTMLInputElement),
  regions: required("overlay-regions", HTMLInputElement),
  shape: required("overlay-shape", HTMLInputElement),
  press: required("overlay-press", HTMLInputElement),
};

const context2d = canvas.getContext("2d");
if (context2d === null) {
  throw new Error("canvas 2D context unavailable");
}
const context: CanvasRenderingContext2D = context2d;

let recording: Recording | null = null;
let playback: Playback | null = null;
let selectedPlayer: number | null = null;
let viewport: Viewport | null = null;

for (const speed of SPEEDS) {
  const option = document.createElement("option");
  option.value = String(speed);
  option.textContent = `${speed}x`;
  option.selected = speed === 1;
  speedSelect.append(option);
}

function setControlsEnabled(enabled: boolean): void {
    for (const control of [
    playButton,
    backButton,
    forwardButton,
    speedSelect,
    slider,
    allTargets,
    ...Object.values(overlayBoxes),
  ]) {
    control.disabled = !enabled;
  }
}

// Matches the canvas's pixel size to its displayed size, so drawing stays
// sharp at any window size and pixel density.
function resizeCanvas(): void {
  const ratio = window.devicePixelRatio || 1;
  const width = Math.max(Math.round(canvas.clientWidth * ratio), 1);
  const height = Math.max(Math.round(canvas.clientHeight * ratio), 1);
  if (canvas.width !== width || canvas.height !== height) {
    canvas.width = width;
    canvas.height = height;
  }
  if (recording !== null) {
    viewport = new Viewport(width, height, recording.pitch.length, recording.pitch.width);
  }
}

function draw(): void {
  if (recording === null || playback === null || viewport === null) {
    return;
  }
  renderFrame(context, viewport, recording, {
    frameIndex: playback.index,
    selectedPlayer,
        showAllTargets: allTargets.checked,
    overlays: {
      pitchControl: overlayBoxes.pitchControl.checked,
      zones: overlayBoxes.zones.checked,
      regions: overlayBoxes.regions.checked,
      shape: overlayBoxes.shape.checked,
      press: overlayBoxes.press.checked,
    },
  });
  renderPanel(panel, recording, playback.index, selectedPlayer);
  const frame = recording.frames[playback.index];
  tickLabel.textContent = `tick ${frame?.tick ?? 0} / ${recording.frames.at(-1)?.tick ?? 0}`;
  slider.value = String(playback.index);
  playButton.textContent = playback.playing ? "Pause" : "Play";
}

function load(text: string, source: string): void {
  try {
    recording = parseRecording(text);
  } catch (error) {
    status.textContent = `${source}: ${(error as Error).message}`;
    return;
  }
  playback = new Playback(recording.frames.length, recording.ticksPerSecond);
  playback.setSpeed(Number(speedSelect.value));
  selectedPlayer = null;
  slider.max = String(recording.frames.length - 1);
  status.textContent =
    `${source}: ${recording.scenario}, seed ${recording.seed}, ` +
    `${recording.frames.length} frames, core ${recording.coreVersion}`;
  setControlsEnabled(true);
  resizeCanvas();
  draw();
}

fileInput.addEventListener("change", () => {
  const file = fileInput.files?.[0];
  if (file !== undefined) {
    file.text().then(
      (text) => load(text, file.name),
      (error: unknown) => {
        status.textContent = `${file.name}: ${String(error)}`;
      },
    );
  }
});

playButton.addEventListener("click", () => {
  playback?.togglePlaying();
  draw();
});
backButton.addEventListener("click", () => {
  playback?.step(-1);
  draw();
});
forwardButton.addEventListener("click", () => {
  playback?.step(1);
  draw();
});
speedSelect.addEventListener("change", () => {
  playback?.setSpeed(Number(speedSelect.value));
});
slider.addEventListener("input", () => {
  playback?.pause();
  playback?.seek(Number(slider.value));
  draw();
});
allTargets.addEventListener("change", draw);
for (const box of Object.values(overlayBoxes)) {
  box.addEventListener("change", draw);
}

canvas.addEventListener("click", (event) => {
  const frame = recording?.frames[playback?.index ?? 0];
  if (frame === undefined || viewport === null) {
    return;
  }
  const bounds = canvas.getBoundingClientRect();
  const ratio = canvas.width / bounds.width;
  const point = viewport.toPitch([
    (event.clientX - bounds.left) * ratio,
    (event.clientY - bounds.top) * ratio,
  ]);
  selectedPlayer = playerAt(frame.players, point, SELECT_DISTANCE_METERS)?.id ?? null;
  draw();
});

// Space plays and pauses, arrows step one tick (ten with Shift), Escape
// clears the selection.
document.addEventListener("keydown", (event) => {
  if (
    playback === null ||
    event.target instanceof HTMLInputElement ||
    event.target instanceof HTMLSelectElement
  ) {
    return;
  }
  const stride = event.shiftKey ? 10 : 1;
  switch (event.key) {
    case " ":
      playback.togglePlaying();
      break;
    case "ArrowLeft":
      playback.step(-stride);
      break;
    case "ArrowRight":
      playback.step(stride);
      break;
    case "Escape":
      selectedPlayer = null;
      break;
    default:
      return;
  }
  event.preventDefault();
  draw();
});

window.addEventListener("resize", () => {
  resizeCanvas();
  draw();
});

let lastTimestamp: number | null = null;
function animate(timestamp: number): void {
  const elapsed = lastTimestamp === null ? 0 : timestamp - lastTimestamp;
  lastTimestamp = timestamp;
  // advance() pauses at the last frame before returning, so this draw also
  // updates the play button.
  if (playback?.advance(elapsed) === true) {
    draw();
  }
  requestAnimationFrame(animate);
}

setControlsEnabled(false);
resizeCanvas();
requestAnimationFrame(animate);

// ?frames=<path> loads a file served next to the page, as `pnpm run serve`
// does for the file it is given.
const framesUrl = new URLSearchParams(window.location.search).get("frames");
if (framesUrl !== null) {
  status.textContent = `loading ${framesUrl}...`;
  fetch(framesUrl)
    .then(async (response) => {
      if (!response.ok) {
        throw new Error(`HTTP ${response.status}`);
      }
      load(await response.text(), framesUrl);
    })
    .catch((error: unknown) => {
      status.textContent = `${framesUrl}: ${String(error)}`;
    });
}
