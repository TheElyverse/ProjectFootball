import assert from "node:assert/strict";
import { test } from "node:test";

import {
  describeEvent,
    frameSeconds,
  isLoggedEvent,
  latestDecision,
  parseRecording,
    sideOf,
} from "../dist/frames.js";
import { fixture } from "./fixture.mjs";

test("parseRecording reads a frame file", () => {
  const recording = parseRecording(JSON.stringify(fixture()));
  assert.equal(recording.frames.length, 3);
  assert.equal(recording.seed, "42");
  assert.equal(sideOf(recording, 2), "away");
  assert.equal(sideOf(recording, 99), undefined);
  assert.equal(frameSeconds(recording, recording.frames[2]), 2 / 30);
});

test("parseRecording rejects other documents with a reason", () => {
  assert.throws(() => parseRecording("{ nope"), /not a JSON document/);
  assert.throws(
    () => parseRecording(JSON.stringify({ format: "replay" })),
    /not a debug frame file/,
  );
  assert.throws(
    () => parseRecording(JSON.stringify({ ...fixture(), version: 2 })),
    /unsupported .* version 2/,
  );
  assert.throws(
    () => parseRecording(JSON.stringify({ ...fixture(), frames: [] })),
    /at least one frame/,
  );
  assert.throws(() => parseRecording(JSON.stringify({ ...fixture(), pitch: {} })), /pitch/);
  assert.throws(
    () => parseRecording(JSON.stringify({ ...fixture(), ticksPerSecond: 0 })),
    /ticksPerSecond/,
  );
});

test("latestDecision finds the decision at or before a frame", () => {
  const recording = parseRecording(JSON.stringify(fixture()));
  assert.equal(latestDecision(recording, 0, 1), undefined);
  assert.equal(latestDecision(recording, 1, 1)?.frameIndex, 1);
  assert.equal(latestDecision(recording, 2, 1)?.frameIndex, 1);
  assert.equal(latestDecision(recording, 2, 2), undefined);
});

test("describeEvent names the players involved", () => {
  assert.equal(
    describeEvent({
      type: "passAttempted",
      passer: 4,
      intendedReceiver: 7,
      from: [0, 0],
      target: [1, 1],
      speed: 9,
    }),
    "#4 passes to #7",
  );
  assert.equal(
    describeEvent({ tick: 0, type: "passIntercepted", interceptor: 9, passer: 4 }),
    "#9 intercepts #4's pass",
  );
  assert.equal(
    describeEvent({ tick: 0, type: "possessionChanged", previousOwner: 4, newOwner: null }),
    "ball free (was #4)",
  );
  assert.equal(
    describeEvent({ tick: 0, type: "phaseChanged", side: "away", previous: null, phase: "pressing" }),
    "away: pressing",
  );
    assert.equal(
    describeEvent({ tick: 0, type: "ballWon", winner: 9, loser: 4, position: [1, 2] }),
    "#9 wins the ball from #4",
  );
    assert.equal(
    describeEvent({
      tick: 0,
      type: "pressingStarted",
      side: "home",
      carrier: 9,
      trigger: "backPass",
      assignments: [{ player: 3, role: "press", subject: 9 }],
    }),
    "home presses #9 (backPass, 1 players)",
  );
  assert.equal(
    describeEvent({ tick: 0, type: "pressingEnded", side: "home", outcome: "ballRegained" }),
        "home press ends: ballRegained",
  );
  assert.equal(
    describeEvent({
      tick: 0,
      type: "tacticChanged",
      side: "away",
      tactic: "pressing",
      contentHash: "0123456789abcdef",
    }),
    "away switches to pressing",
  );
    assert.equal(describeEvent({ tick: 0, type: "somethingNew" }), "somethingNew");
  assert.equal(
    describeEvent({ tick: 0, type: "pitchControlSampled", homeShare: 0.614, ball: [30, 20] }),
    "home controls 61% of the pitch",
  );
});

test("the event log leaves out pitch control samples", () => {
  assert.equal(isLoggedEvent({ tick: 0, type: "pitchControlSampled", homeShare: 0.5, ball: [0, 0] }), false);
  assert.equal(isLoggedEvent({ tick: 0, type: "ballWon", winner: 1, loser: 2, position: [0, 0] }), true);
});
