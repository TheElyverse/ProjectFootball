import { FaDev as DevIcon, FaGamepad as GameIcon } from "react-icons/fa";
import { useFocusInput } from "./navigation/focus";
import { useScreenController } from "./screens/screen";
import { ScreenProvider } from "./screens/ScreenProvider";
import { MainMenu } from "./screens/MainMenu/MainMenu";
import { Manager } from "./screens/Manager/Manager";
import { isDev } from "@/utils/env";
import { DevScreen } from "@/screens/dev/DevScreen";

// Navigation lives entirely in the UI: Unreal only hosts the page, keeps it alive across
// map changes and says which route to show first ("/" is the main menu, "/<view>" a view
// of the manager screen). A screen fills the whole page; the manager screen shows one of
// its views.
export function Game() {
  return (
    <ScreenProvider>
      {isDev() && <DevScreenToggle />}
      <ActiveScreen />
    </ScreenProvider>
  );
}

function ActiveScreen() {
  const { screen } = useScreenController();
  useFocusInput();

  // Nothing is shown until the game navigates.
  if (screen === undefined) {
    return null;
  }
  switch (screen.kind) {
    case "mainMenu":
      return <MainMenu />;
    case "dev":
      return <DevScreen initialView={screen.view} />;
    case "manager":
      // When the game navigates to another view of the manager screen, its view history starts anew.
      return <Manager key={screen.view} initialView={screen.view} />;
  }
}

function DevScreenToggle() {
  const { navigate, screen } = useScreenController();
  const Icon = screen?.kind !== "dev" ? DevIcon : GameIcon;
  return (
    <div className="fixed right-2 top-1/2 -translate-y-1/2">
      <button
        className="hover:text-volt"
        onClick={() => {
          if (screen?.kind === "dev") {
            navigate({ kind: "mainMenu" });
          } else {
            navigate({ kind: "dev", view: "dev.components.overview" });
          }
        }}
      >
        <Icon className="h-8 w-12" />
      </button>
    </div>
  );
}
