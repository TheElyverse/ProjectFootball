import {
    describeEvent,
  isLoggedEvent,
  frameSeconds,
  latestDecision,
  sideOf,
  type Frame,
  type Observation,
  type PlayerFrame,
  type Recording,
} from "./frames.js";

// The side panel as plain DOM: text only, never HTML from the file.

const EVENT_LOG_LENGTH = 12;

function element<K extends keyof HTMLElementTagNameMap>(
  tag: K,
  text?: string,
  className?: string,
): HTMLElementTagNameMap[K] {
  const node = document.createElement(tag);
  if (text !== undefined) {
    node.textContent = text;
  }
  if (className !== undefined) {
    node.className = className;
  }
  return node;
}

function heading(text: string): HTMLElement {
  return element("h2", text);
}

function definitionList(entries: readonly (readonly [string, string])[]): HTMLElement {
  const list = element("dl");
  for (const [term, value] of entries) {
    list.append(element("dt", term), element("dd", value));
  }
  return list;
}

function table(headers: readonly string[], rows: readonly (readonly string[])[]): HTMLElement {
  const node = element("table");
  const head = element("tr");
  for (const header of headers) {
    head.append(element("th", header));
  }
  node.append(head);
  for (const row of rows) {
    const tr = element("tr");
    for (const cell of row) {
      tr.append(element("td", cell));
    }
    node.append(tr);
  }
  return node;
}

const vector = (value: readonly [number, number]): string =>
  `${value[0].toFixed(1)}, ${value[1].toFixed(1)}`;
const number = (value: number, digits = 2): string => value.toFixed(digits);
const player = (id: number | null): string => (id === null ? "-" : `#${id}`);

function matchSection(recording: Recording, frame: Frame): HTMLElement[] {
  return [
    heading("Match"),
    definitionList([
      ["scenario", recording.scenario],
      ["seed", recording.seed],
      ["tick", `${frame.tick} (${number(frameSeconds(recording, frame), 2)} s)`],
      ["state hash", frame.stateHash],
      ["ball owner", player(frame.ball.owner)],
      ["last touch", player(frame.ball.lastTouch)],
      ["ball speed", `${number(Math.hypot(...frame.ball.velocity), 1)} m/s`],
    ]),
  ];
}

// The latest events up to the current frame, newest first.
function eventSection(recording: Recording, frameIndex: number): HTMLElement[] {
  const lines: HTMLElement[] = [];
  for (let index = frameIndex; index >= 0 && lines.length < EVENT_LOG_LENGTH; index -= 1) {
    const frame = recording.frames[index];
        for (const event of [...(frame?.events ?? [])].reverse().filter(isLoggedEvent)) {
      if (lines.length < EVENT_LOG_LENGTH) {
        lines.push(
          element(
            "li",
            `${event.tick}: ${describeEvent(event)}`,
            index === frameIndex ? "current" : undefined,
          ),
        );
      }
    }
  }
  const list = element("ul", undefined, "events");
  list.append(...lines);
  return [heading("Events"), lines.length === 0 ? element("p", "none yet", "muted") : list];
}

function observationRows(observations: readonly Observation[]): string[][] {
  return observations.map((observation) => [
    observation.entity === "ball" ? "ball" : `#${observation.entity}`,
    vector(observation.position),
    number(observation.confidence),
    String(observation.lastSeen),
  ]);
}

function selectionSection(
  recording: Recording,
  frameIndex: number,
  selected: PlayerFrame,
): HTMLElement[] {
  const nodes: HTMLElement[] = [
    heading(`Player #${selected.id} (${sideOf(recording, selected.id) ?? "?"})`),
    definitionList([
      ["position", vector(selected.position)],
      ["speed", `${number(Math.hypot(...selected.velocity), 1)} m/s`],
      ["target", selected.target === null ? "-" : vector(selected.target)],
    ]),
    element("h3", `Observations (${selected.observations.length})`),
    table(["entity", "position", "confidence", "seen"], observationRows(selected.observations)),
  ];

  const latest = latestDecision(recording, frameIndex, selected.id);
  if (latest === undefined) {
    nodes.push(element("h3", "Pass decision"), element("p", "no decision yet", "muted"));
    return nodes;
  }
  const { decision } = latest;
  nodes.push(
    element(
      "h3",
      `Pass decision at tick ${decision.tick}: ${decision.outcome === "passed" ? "passed" : "no valid option"}`,
    ),
    table(
      ["to", "dist", "risk", "compl", "prog", "press", "utility", "status"],
      decision.candidates.map((candidate, index) => [
        `#${candidate.receiver}${decision.chosen === index ? " *" : ""}`,
        number(candidate.distance, 1),
        number(candidate.interceptionRisk),
        number(candidate.completion),
        number(candidate.progression),
        number(candidate.receiverPressure),
        number(candidate.utility),
        candidate.rejection,
      ]),
    ),
  );
  if (decision.candidates.length === 0) {
    nodes.push(element("p", "no teammate remembered", "muted"));
  }
  return nodes;
}

// Replaces the panel's contents with the current frame's details.
export function renderPanel(
  panel: HTMLElement,
  recording: Recording,
  frameIndex: number,
  selectedPlayer: number | null,
): void {
  const frame = recording.frames[frameIndex];
  if (frame === undefined) {
    panel.replaceChildren();
    return;
  }
  const selected = frame.players.find((entry) => entry.id === selectedPlayer);
  panel.replaceChildren(
    ...matchSection(recording, frame),
    ...(selected === undefined
      ? [
          element(
            "p",
            "Click a player to see his target, vision, observations and pass decision.",
            "muted",
          ),
        ]
      : selectionSection(recording, frameIndex, selected)),
    ...eventSection(recording, frameIndex),
  );
}
