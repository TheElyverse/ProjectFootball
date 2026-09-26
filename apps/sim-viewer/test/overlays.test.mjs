import assert from "node:assert/strict";
import { test } from "node:test";

import { parseRecording } from "../dist/frames.js";
import { Viewport } from "../dist/geometry.js";
import { controlColor, drawActionCandidates } from "../dist/overlays.js";
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
