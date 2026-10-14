import { Squad } from "./Squad/Squad";
import { ViewComponent } from "@/screens/screen";

// The manager screen's views in navigation order. A new view is one entry here.
export const managerViews = [
  { id: "squad", title: "view.squad", Component: Squad },
] as const satisfies readonly ViewComponent[];

export type ManagerViewId = (typeof managerViews)[number]["id"];
