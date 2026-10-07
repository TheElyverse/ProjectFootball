import { useScreenController } from "@/screens/screen";
import { useViewHistory } from "@/navigation/viewHistory";
import { ManagerLayout } from "./ManagerLayout";
import { managerViews, type ManagerViewId } from "./views";

// The manager screen: the layout around the active view. Back returns to the previous
// view but never leaves the screen; the menu button does.
export function Manager({ initialView }: { initialView: ManagerViewId }) {
  const { navigate } = useScreenController();
  const { view, open } = useViewHistory(initialView);
  const { Component } =
    managerViews.find((candidate) => candidate.id === view) ?? managerViews[0];
  return (
    <ManagerLayout
      activeView={view}
      onNavigate={open}
      onMainMenu={() => navigate({ kind: "mainMenu" })}
    >
      <Component />
    </ManagerLayout>
  );
}
