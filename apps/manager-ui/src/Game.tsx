import { useCallback, useEffect, useState } from "react";
import { useNavigationInput } from "./navigation/focus";
import { MainMenu } from "./screens/MainMenu/MainMenu";
import { Manager } from "./screens/Manager/Manager";
import { managerViews } from "./screens/Manager/views";
import { command, onGameEvent } from "./ue/bridge";
import { parseScreen, Screen } from "./screens/screen";

// Navigation lives entirely in the UI: Unreal only hosts the page, keeps it alive across
// map changes and says which route to show first ("/" is the main menu, "/<view>" a view
// of the manager screen). A screen fills the whole page; the manager screen shows one of
// its views.
export function Game() {
  // Nothing is shown until the game navigates.
  const [screen, setScreen] = useState<Screen>();

  useEffect(() => {
    const unsubscribe = onGameEvent("navigate", (route) => {
      const next = parseScreen(route);
      if (next === undefined) {
        console.error(`The game navigated to the unknown route "${route}"`);
        return;
      }
      setScreen(next);
    });
    void command("ready");
    return unsubscribe;
  }, []);

  const back = useCallback(() => {
    if (screen?.kind === "manager") {
      setScreen({ kind: "mainMenu" });
    }
  }, [screen]);
  useNavigationInput(back);

  if (screen === undefined) {
    return null;
  }
  if (screen.kind === "mainMenu") {
    return (
      <MainMenu
        onStartGame={() =>
          setScreen({ kind: "manager", view: managerViews[0].id })
        }
      />
    );
  }
  return (
    <Manager
      view={screen.view}
      onNavigate={(view) => setScreen({ kind: "manager", view })}
      onMainMenu={() => setScreen({ kind: "mainMenu" })}
    />
  );
}
