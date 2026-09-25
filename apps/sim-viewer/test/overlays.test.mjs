import assert from "node:assert/strict";
import { test } from "node:test";

import { controlColor } from "../dist/overlays.js";

test("controlColor shows who controls a cell and how clearly", () => {
  assert.equal(controlColor(0.5), "rgba(79, 142, 247, 0.000)");
  assert.equal(controlColor(0.75), "rgba(79, 142, 247, 0.300)");
  assert.equal(controlColor(0.25), "rgba(229, 83, 75, 0.300)");
  // Clear control never hides the grass completely.
  assert.equal(controlColor(1), "rgba(79, 142, 247, 0.600)");
  assert.equal(controlColor(0), "rgba(229, 83, 75, 0.600)");
});
