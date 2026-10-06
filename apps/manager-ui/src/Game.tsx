import { useCallback, useEffect, useState } from "react";
import { ManagerLayout } from "./components/ManagerLayout";
import { useNavigationInput } from "./navigation/focus";
import { managerScreens, type ManagerScreenId } from "./screens";
import { MainMenu } from "./screens/MainMenu/MainMenu";
import { command, onGameEvent } from "./ue/bridge";

// Navigation lives entirely in the UI: Unreal only hosts the page, keeps it alive across
// map changes and says which route to show first ("/" is the main menu, "/<screen>" a
// manager screen).
type Route =
  { kind: "mainMenu" } | { kind: "manager"; screen: ManagerScreenId };

function parseRoute(path: string): Route | undefined {
  if (path === "/") {
    return { kind: "mainMenu" };
  }
  const screen = managerScreens.find((screen) => `/${screen.id}` === path);
  return screen && { kind: "manager", screen: screen.id };
}

export function Game() {
  // Nothing is shown until the game navigates.
  const [route, setRoute] = useState<Route>();

  useEffect(() => {
    const unsubscribe = onGameEvent("navigate", (path) => {
      const next = parseRoute(path);
      if (next === undefined) {
        console.error(`The game navigated to the unknown route "${path}"`);
        return;
      }
      setRoute(next);
    });
    void command("ready");
    return unsubscribe;
  }, []);

  const back = useCallback(() => {
    if (route?.kind === "manager") {
      setRoute({ kind: "mainMenu" });
    }
  }, [route]);
  useNavigationInput(back);

  if (route === undefined) {
    return null;
  }
  if (route.kind === "mainMenu") {
    return (
      <MainMenu
        onStartGame={() =>
          setRoute({ kind: "manager", screen: managerScreens[0].id })
        }
      />
    );
  }
  const { Component } =
    managerScreens.find((screen) => screen.id === route.screen) ??
    managerScreens[0];
  return (
    <ManagerLayout
      activeScreen={route.screen}
      onNavigate={(screen) => setRoute({ kind: "manager", screen })}
      onMainMenu={() => setRoute({ kind: "mainMenu" })}
    >
      <Component />
    </ManagerLayout>
  );
}
