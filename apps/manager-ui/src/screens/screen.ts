import { createContext, useContext } from "react";
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

export interface ScreenController {
  // Undefined until the game navigates.
  screen: Screen | undefined;
  navigate: (screen: Screen) => void;
}

export const ScreenContext = createContext<ScreenController | undefined>(
  undefined,
);

export function useScreenController(): ScreenController {
  const controller = useContext(ScreenContext);
  if (controller === undefined) {
    throw new Error("useScreenController needs a ScreenProvider");
  }
  return controller;
}
