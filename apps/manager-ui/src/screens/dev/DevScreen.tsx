import { DevViewId, devViews } from "@/screens/dev/index";
import { useViewHistory } from "@/navigation/viewHistory";

export function DevScreen({ initialView }: { initialView: DevViewId }) {
  const { view } = useViewHistory(initialView);
  const { Component } =
    devViews.find((candidate) => candidate.id === view) ?? devViews[0];

  return (
    <div className="flex flex-col">
      <div className="px-4 py-2 font-headline text-4xl bg-top-bar border-b border-b-touchline">
        Dev Tools
      </div>
      <Component />
    </div>
  );
}
