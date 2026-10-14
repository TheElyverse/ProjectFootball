import { FormattedMessage } from "react-intl";

export function Components() {
  return (
    <div className="bg-night-pitch flex flex-col">
      <div className="px-4 py-2 font-headline text-2xl border-b border-b-touchline">
        <FormattedMessage id="dev.components.overview.title" />
      </div>
      <div className="px-4 py-2 grow">body</div>
    </div>
  );
}
