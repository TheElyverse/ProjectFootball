import { type ManagerViewId, managerViews } from "./Manager/views";

export type Screen =
  { kind: "mainMenu" } | { kind: "manager"; view: ManagerViewId };

export function parseScreen(route: string): Screen | undefined {
  if (route === "/") {
    return { kind: "mainMenu" };
  }
  const view = managerViews.find((view) => `/${view.id}` === route);
  return view && { kind: "manager", view: view.id };
}
