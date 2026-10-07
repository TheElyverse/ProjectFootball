import { useIntl } from "react-intl";
import { command, isUnrealHost } from "@/ue/bridge";
import { Brand } from "@/components/Logo/Brand";
import { MainMenuButton } from "@/components/Button/MainMenuButton";
import { useScreenController } from "@/screens/screen";
import { managerViews } from "@/screens/Manager/views";

// A 640 px (40 rem) column on the left over the 3D scene. The page is transparent, so the scrim is a
// gradient: CEF renders the page on its own, and backdrop blur cannot reach the scene.
export function MainMenu() {
  const intl = useIntl();
  const { navigate } = useScreenController();

  return (
    <div className="scrim-main-menu h-full">
      <div className="flex h-full w-160 flex-col gap-16 px-16 py-20">
        <Brand />
        <nav className="flex flex-col gap-2">
          <MainMenuButton
            autoFocus
            text={intl.formatMessage({ id: "mainMenu.newCareer" })}
            onClick={() =>
              navigate({ kind: "manager", view: managerViews[0].id })
            }
          />
          <MainMenuButton
            text={intl.formatMessage({ id: "mainMenu.loadGame" })}
          />
          <MainMenuButton
            text={intl.formatMessage({ id: "mainMenu.settings" })}
          />
          {isUnrealHost() && (
            <MainMenuButton
              text={intl.formatMessage({ id: "mainMenu.quit" })}
              onClick={() => void command("quit")}
            />
          )}
        </nav>
      </div>
    </div>
  );
}
