import { useState } from "react";
import { ManagerLayout } from "./components/ManagerLayout";
import { managerScreens, type ManagerScreenId } from "./screens";
import { MainMenu } from "./screens/MainMenu";

// Navigation lives entirely in the UI: Unreal only hosts the page and keeps it alive
// across map changes.
type Route = { kind: "mainMenu" } | { kind: "manager"; screen: ManagerScreenId };

export function App() {
  const [route, setRoute] = useState<Route>({ kind: "mainMenu" });

  if (route.kind === "mainMenu") {
    return <MainMenu onStartGame={() => setRoute({ kind: "manager", screen: managerScreens[0].id })} />;
  }

  const { Component } = managerScreens.find((screen) => screen.id === route.screen) ?? managerScreens[0];
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
