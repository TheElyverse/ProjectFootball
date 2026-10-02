import type { ComponentType } from "react";
import { SquadScreen } from "./SquadScreen";

// The manager screens in navigation order. A new screen is one entry here.
export const managerScreens = [{ id: "squad", title: "Squad", Component: SquadScreen }] as const satisfies readonly {
  id: string;
  title: string;
  Component: ComponentType;
}[];

export type ManagerScreenId = (typeof managerScreens)[number]["id"];
