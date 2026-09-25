import assert from "node:assert/strict";
import { test } from "node:test";

import { playerAt, Viewport, visionCone } from "../dist/geometry.js";

const close = (actual, expected) =>
  assert.ok(Math.abs(actual - expected) < 1e-9, `${actual} != ${expected}`);

test("Viewport fits the pitch with its margin and maps both ways", () => {
  // 66 x 46 meters with the margin: the height limits the scale to 10 px/m.
  const viewport = new Viewport(1000, 460, 60, 40);
  close(viewport.scale, 10);
  const [x, y] = viewport.toCanvas([0, 0]);
  close(x, 200);
  close(y, 30);
  const [pitchX, pitchY] = viewport.toPitch(viewport.toCanvas([12.5, 7.25]));
  close(pitchX, 12.5);
  close(pitchY, 7.25);
});

test("visionCone spans the field of view around the facing", () => {
  const cone = visionCone([10, 10], [1, 0], 5, 180, 2);
  assert.deepEqual(cone[0], [10, 10]);
  assert.equal(cone.length, 4);
  // From straight up (-y) over straight ahead to straight down (+y).
  close(cone[1][0], 10);
  close(cone[1][1], 5);
  close(cone[2][0], 15);
  close(cone[3][1], 15);
});

test("visionCone of 360 degrees is a closed circle without the apex", () => {
  const cone = visionCone([0, 0], [0, 1], 2, 360, 4);
  assert.equal(cone.length, 5);
  for (const [x, y] of cone) {
    close(Math.hypot(x, y), 2);
  }
});

test("playerAt picks the closest player within reach", () => {
  const players = [
    { id: 1, position: [0, 0] },
    { id: 2, position: [3, 0] },
  ];
  assert.equal(playerAt(players, [1, 0], 2)?.id, 1);
  assert.equal(playerAt(players, [2.2, 0], 2)?.id, 2);
  assert.equal(playerAt(players, [10, 10], 2), undefined);
});
