// The only place the UI talks to the game. Inside Unreal, UManagerUISubsystem binds a
// UManagerBridge as window.ue.manager (Unreal lower-cases all bound names) and every
// call returns a promise. The game calls back through window.ui: navigate(route) shows a
// route, input(action) forwards keyboard and gamepad navigation. In a plain browser there
// is no binding, so mocks stand in for development.

import type { Messages } from "../i18n";

export interface SquadRow {
  name: string;
  position: string;
  age: number;
  attributes: number[];
  fitness: number;
  contractYears: number;
  marketValue: number;
}

export interface SquadView {
  attributeNames: string[];
  rows: SquadRow[];
}

// The UI texts in the game's language, from Content/ManagerUI/locales/<locale>.json. Texts
// it lacks fall back to the bundled English ones.
export interface MessagesView {
  locale: string;
  messages: Partial<Messages>;
}

// Every query the game answers, with the view model it returns.
export interface Queries {
  squad: SquadView;
  messages: MessagesView;
}

// "ready" tells the game that window.ui takes calls; it answers with navigate.
export type Command = "ready" | "quit";

// The navigation inputs the game forwards; the page itself decides what they focus.
export type NavigationInput = "nav.up" | "nav.down" | "nav.confirm" | "nav.back";

// Everything the game sends the page, with its argument.
export interface GameEvents {
  navigate: string;
  input: NavigationInput;
}

interface UnrealManagerBinding {
  query(name: string): Promise<unknown>;
  command(name: string): Promise<unknown>;
}

declare global {
  interface Window {
    ue?: { manager?: UnrealManagerBinding };
    ui: { [Event in keyof GameEvents]: (value: GameEvents[Event]) => void };
  }
}

export const isUnrealHost = (): boolean => window.ue?.manager !== undefined;

export async function query<Name extends keyof Queries>(
  name: Name,
): Promise<Queries[Name]> {
  const binding = window.ue?.manager;
  if (binding === undefined) {
    return mockQueries[name]();
  }
  // A single return value arrives either bare or wrapped as { ReturnValue }.
  const result = await binding.query(name);
  const json =
    typeof result === "object" && result !== null && "ReturnValue" in result
      ? (result as { ReturnValue: string }).ReturnValue
      : (result as string);
  if (json === "") {
    throw new Error(`The game does not know the query "${name}"`);
  }
  return JSON.parse(json) as Queries[Name];
}

export async function command(name: Command): Promise<void> {
  const binding = window.ue?.manager;
  if (binding === undefined) {
    console.info(`Mock command: ${name}`);
    if (name === "ready") {
      window.ui.navigate("/");
    }
    return;
  }
  await binding.command(name);
}

type Listener<Event extends keyof GameEvents> = (
  value: GameEvents[Event],
) => void;

const listeners: { [Event in keyof GameEvents]: Set<Listener<Event>> } = {
  navigate: new Set(),
  input: new Set(),
};

// Subscribes to a game event; returns the unsubscribe function.
export function onGameEvent<Event extends keyof GameEvents>(
  event: Event,
  listener: Listener<Event>,
): () => void {
  listeners[event].add(listener);
  return () => listeners[event].delete(listener);
}

function emit<Event extends keyof GameEvents>(
  event: Event,
  value: GameEvents[Event],
) {
  for (const listener of listeners[event]) {
    listener(value);
  }
}

window.ui = {
  navigate: (route) => emit("navigate", route),
  input: (action) => emit("input", action),
};

// Inside Unreal the game takes these keys before the page sees them; the mock maps them
// the same way, so navigation behaves alike in both.
const mockNavigationKeys: Partial<Record<string, NavigationInput>> = {
  ArrowUp: "nav.up",
  ArrowDown: "nav.down",
  Enter: "nav.confirm",
  " ": "nav.confirm",
  Escape: "nav.back",
};

if (!isUnrealHost()) {
  window.addEventListener("keydown", (event) => {
    const input = mockNavigationKeys[event.key];
    if (input === undefined) {
      return;
    }
    event.preventDefault();
    if (input === "nav.up" || input === "nav.down" || !event.repeat) {
      window.ui.input(input);
    }
  });
}

const mockQueries: {
  [Name in keyof Queries]: () => Queries[Name] | Promise<Queries[Name]>;
} = {
  squad: () => mockSquad(500),
  messages: mockMessages,
};

// Takes the first browser language with a catalog, falling back to English like the game.
async function mockMessages(): Promise<MessagesView> {
  for (const language of navigator.languages) {
    const locale = language.split("-")[0] ?? language;
    const response = await fetch(`locales/${locale}.json`);
    if (response.ok) {
      return { locale, messages: (await response.json()) as Messages };
    }
  }
  return { locale: "en", messages: {} };
}

function mockSquad(count: number): SquadView {
  const attributeNames = Array.from(
    { length: 16 },
    (_, index) => `Attr ${index + 1}`,
  );
  const rows = Array.from({ length: count }, (_, index): SquadRow => {
    const attributes = attributeNames.map(
      (_, attribute) => ((index * 7 + attribute * 13) % 20) + 1,
    );
    return {
      name: `Mock Player ${index + 1}`,
      position: ["GK", "DC", "MC", "ST"][index % 4] ?? "MC",
      age: 16 + (index % 23),
      attributes,
      fitness: 60 + (index % 41),
      contractYears: index % 6,
      marketValue: attributes.reduce((sum, value) => sum + value, 0) * 25_000,
    };
  });
  return { attributeNames, rows };
}
