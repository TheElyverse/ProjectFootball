import assert from "node:assert/strict";
import { test } from "node:test";

import { parseRecording } from "../dist/frames.js";
import { Viewport } from "../dist/geometry.js";
import {
  controlColor,
  drawActionCandidates,
  keeperAssignments,
  setPieceAssignments,
} from "../dist/overlays.js";
import { fixture } from "./fixture.mjs";

test("controlColor shows who controls a cell and how clearly", () => {
  assert.equal(controlColor(0.5), "rgba(79, 142, 247, 0.000)");
  assert.equal(controlColor(0.75), "rgba(79, 142, 247, 0.300)");
  assert.equal(controlColor(0.25), "rgba(229, 83, 75, 0.300)");
  // Clear control never hides the grass completely.
  assert.equal(controlColor(1), "rgba(79, 142, 247, 0.600)");
  assert.equal(controlColor(0), "rgba(229, 83, 75, 0.600)");
});

// Records where every line starts, in canvas pixels.
function recordingContext() {
  const starts = [];
  return {
    starts,
    strokeStyle: "",
    lineWidth: 0,
    beginPath() {},
    moveTo(x, y) {
      starts.push([x, y]);
    },
    lineTo() {},
    arc() {},
    stroke() {},
    setLineDash() {},
  };
}

test("drawActionCandidates starts the candidate lines where the player decided", () => {
  // A frame's state is the one after the step that recorded its diagnostics:
  // the fixture's action decision is in frame 1 but was taken at tick 0, so
  // the lines belong at frame 0's position, not frame 1's.
  const document = fixture();
  document.frames[0].players[1].position = [40, 20];
  document.frames[1].players[1].position = [44, 26];
  const recording = parseRecording(JSON.stringify(document));
  const viewport = new Viewport(600, 400, 60, 40);
  const context = recordingContext();

  drawActionCandidates(context, viewport, recording, 1, 2);

  assert.equal(context.starts.length, 1);
  assert.deepEqual(viewport.toPitch(context.starts[0]), [40, 20]);
});

test("setPieceAssignments joins every player with a place to it, by side", () => {
  const recording = parseRecording(JSON.stringify(fixture()));
  assert.deepEqual(setPieceAssignments(recording, recording.frames[0]), []);
  assert.deepEqual(setPieceAssignments(recording, recording.frames[2]), [
    { side: "home", from: [20, 20], to: [59.5, 0.5] },
    { side: "away", from: [40, 20], to: [55, 20] },
  ]);
});

test("keeperAssignments runs from the goal line to where the keeper wants to be", () => {
  const recording = parseRecording(JSON.stringify(fixture()));
  assert.deepEqual(keeperAssignments(recording, recording.frames[2]), [
    { side: "away", from: [60, 19], to: [58.5, 19.5] },
  ]);
});
