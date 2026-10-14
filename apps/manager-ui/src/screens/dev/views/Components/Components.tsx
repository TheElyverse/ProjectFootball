import { FormattedMessage } from "react-intl";
import { MainMenuButton } from "@/components/Button/MainMenuButton";

export function Components() {
  return (
    <div className="bg-night-pitch flex flex-col">
      <div className="px-4 py-2 font-headline text-2xl border-b border-b-touchline">
        <FormattedMessage id="dev.components.overview.title" />
      </div>
      <div className="px-4 py-2 grow">
        <div className="flex flex-col">
          <div>Main Menu</div>
          <div className="flex flex-col gap-4 w-96">
            <MainMenuButton
              text="Continue"
              autoFocus={true}
              hint="VfB Stuttgart - Tuesday, 27th October 2026"
            />
            <MainMenuButton text="New Career" />
            <MainMenuButton text="New Career" level={2} />
          </div>
        </div>
      </div>
    </div>
  );
}
