import { useFocusInput } from "./navigation/focus";
import { useScreenController } from "./screens/screen";
import { ScreenProvider } from "./screens/ScreenProvider";
import { MainMenu } from "./screens/MainMenu/MainMenu";
import { Manager } from "./screens/Manager/Manager";

// Navigation lives entirely in the UI: Unreal only hosts the page, keeps it alive across
// map changes and says which route to show first ("/" is the main menu, "/<view>" a view
// of the manager screen). A screen fills the whole page; the manager screen shows one of
// its views.
export function Game() {
  return (
    <ScreenProvider>
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
  if (screen.kind === "mainMenu") {
    return <MainMenu />;
  }
  // When the game navigates to another view of the manager screen, its view history starts anew.
  return <Manager key={screen.view} initialView={screen.view} />;
}
