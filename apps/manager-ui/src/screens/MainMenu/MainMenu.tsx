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
          {/* TODO: Later, only shown if there is a saved career. */}
          <MainMenuButton
            autoFocus
            text={intl.formatMessage({ id: "mainMenu.continueCareer" })}
            hint={intl.formatMessage({ id: "mainMenu.continueCareer.hint" })}
            onClick={() =>
              navigate({ kind: "manager", view: managerViews[0].id })
            }
          />
          <MainMenuButton
            text={intl.formatMessage({ id: "mainMenu.newCareer" })}
            hint={intl.formatMessage({ id: "mainMenu.newCareer.hint" })}
          />
          <MainMenuButton
            text={intl.formatMessage({ id: "mainMenu.loadCareer" })}
            hint={intl.formatMessage({ id: "mainMenu.loadCareer.hint" })}
          />
          <hr className="m-4 ml-8 text-touchline" />
          <MainMenuButton
            level={2}
            text={intl.formatMessage({ id: "mainMenu.settings" })}
            hint={intl.formatMessage({ id: "mainMenu.settings.hint" })}
          />
          <MainMenuButton
            level={2}
            text={intl.formatMessage({ id: "mainMenu.quit" })}
            hint={intl.formatMessage({ id: "mainMenu.quit.hint" })}
            onClick={() => {
              if (isUnrealHost()) void command("quit");
            }}
          />
        </nav>
      </div>
    </div>
  );
}
