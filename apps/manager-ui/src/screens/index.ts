import type { ComponentType } from "react";
import type { MessageId } from "../i18n";
import { SquadScreen } from "./SquadScreen";

// The manager screens in navigation order. A new screen is one entry here.
export const managerScreens = [{ id: "squad", title: "screen.squad", Component: SquadScreen }] as const satisfies readonly {
  id: string;
  title: MessageId;
  Component: ComponentType;
}[];

export type ManagerScreenId = (typeof managerScreens)[number]["id"];
