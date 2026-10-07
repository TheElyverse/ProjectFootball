import type { Messages } from "@/i18n";
import { MessagesView, Queries, SquadRow, SquadView } from "./bridge";

export const mockQueries: {
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
    // Vite answers a missing catalog with index.html and 200, not with 404.
    if (response.headers.get("content-type")?.includes("json")) {
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
