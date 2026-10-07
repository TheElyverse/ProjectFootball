import { type ReactNode, useEffect, useMemo, useState } from "react";
import { command, onGameEvent } from "@/ue/bridge";
import { parseScreen, type Screen, ScreenContext } from "./screen";

// Holds the screen that fills the page. Switching screens keeps no history: back only
// returns to earlier views within a screen (see useViewHistory).
export function ScreenProvider({ children }: { children: ReactNode }) {
  const [screen, setScreen] = useState<Screen>();

  useEffect(() => {
    const unsubscribe = onGameEvent("navigate", (route) => {
      const nextScreen = parseScreen(route);
      if (nextScreen === undefined) {
        console.error(`game navigated to unknown route "${route}"`);
        // Keep the shown screen, but never leave the page empty: the game has
        // already handed input to the UI.
        setScreen((current) => current ?? { kind: "mainMenu" });
        return;
      }
      setScreen(nextScreen);
    });
    void command("ready");
    return unsubscribe;
  }, []);

  const controller = useMemo(
    () => ({ screen, navigate: (next: Screen) => setScreen(next) }),
    [screen],
  );
  return <ScreenContext value={controller}>{children}</ScreenContext>;
}
