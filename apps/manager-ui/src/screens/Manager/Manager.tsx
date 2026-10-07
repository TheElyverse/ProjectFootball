import { ManagerLayout } from "./ManagerLayout";
import { managerViews, type ManagerViewId } from "./views";

interface ManagerProps {
  view: ManagerViewId;
  onNavigate: (view: ManagerViewId) => void;
  onMainMenu: () => void;
}

// The manager screen: the layout around the active view.
export function Manager({ view, onNavigate, onMainMenu }: ManagerProps) {
  const { Component } =
    managerViews.find((candidate) => candidate.id === view) ?? managerViews[0];
  return (
    <ManagerLayout
      activeView={view}
      onNavigate={onNavigate}
      onMainMenu={onMainMenu}
    >
      <Component />
    </ManagerLayout>
  );
}
