import type { ComponentType } from "react";
import type { MessageId } from "@/i18n";
import { Squad } from "./Squad/Squad";

// The manager screen's views in navigation order. A new view is one entry here.
export const managerViews = [
  { id: "squad", title: "view.squad", Component: Squad },
] as const satisfies readonly {
  id: string;
  title: MessageId;
  Component: ComponentType;
}[];

export type ManagerViewId = (typeof managerViews)[number]["id"];
