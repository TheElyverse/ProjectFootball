import assert from "node:assert/strict";
import { test } from "node:test";

import { Playback } from "../dist/playback.js";

test("Playback advances only while playing, at the tick rate times the speed", () => {
  const playback = new Playback(100, 30);
  assert.equal(playback.advance(1000), false);
  assert.equal(playback.index, 0);

  playback.play();
  playback.advance(1000);
  assert.equal(playback.index, 30);

  playback.setSpeed(2);
  playback.advance(500);
  assert.equal(playback.index, 60);
});

test("Playback keeps partial frames for the next call", () => {
  const playback = new Playback(100, 10);
  playback.play();
  playback.advance(50);
  assert.equal(playback.index, 0);
  playback.advance(50);
  assert.equal(playback.index, 1);
});

test("Playback stops at the last frame and restarts from the first", () => {
  const playback = new Playback(10, 30);
  playback.play();
  assert.equal(playback.advance(10_000), true);
  assert.equal(playback.index, 9);
  assert.equal(playback.playing, false);
  playback.play();
  assert.equal(playback.index, 0);
});

test("step moves single ticks, pauses, and stays inside the recording", () => {
  const playback = new Playback(5, 30);
  playback.play();
  playback.step(1);
  assert.equal(playback.playing, false);
  assert.equal(playback.index, 1);
  playback.step(-3);
  assert.equal(playback.index, 0);
  playback.seek(99);
  assert.equal(playback.index, 4);
});

test("Playback rejects invalid settings", () => {
  assert.throws(() => new Playback(0, 30));
  assert.throws(() => new Playback(5, 0));
  assert.throws(() => new Playback(5, 30).setSpeed(0));
});
