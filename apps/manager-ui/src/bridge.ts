// The only place the UI talks to the game. Inside Unreal, UManagerUISubsystem binds a
// UManagerBridge as window.ue.manager (Unreal lower-cases all bound names) and every
// call returns a promise. In a plain browser there is no binding, so mocks stand in for
// development.

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

// Every query the game answers, with the view model it returns.
export interface Queries {
  squad: SquadView;
}

export type Command = "quit";

interface UnrealManagerBinding {
  query(name: string): Promise<unknown>;
  command(name: string): Promise<unknown>;
}

declare global {
  interface Window {
    ue?: { manager?: UnrealManagerBinding };
  }
}

export const isUnrealHost = (): boolean => window.ue?.manager !== undefined;

export async function query<Name extends keyof Queries>(name: Name): Promise<Queries[Name]> {
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
    return;
  }
  await binding.command(name);
}

const mockQueries: { [Name in keyof Queries]: () => Queries[Name] } = {
  squad: () => mockSquad(500),
};

function mockSquad(count: number): SquadView {
  const attributeNames = Array.from({ length: 16 }, (_, index) => `Attr ${index + 1}`);
  const rows = Array.from({ length: count }, (_, index): SquadRow => {
    const attributes = attributeNames.map((_, attribute) => ((index * 7 + attribute * 13) % 20) + 1);
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
