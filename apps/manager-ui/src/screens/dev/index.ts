import type { ViewComponent } from "@/screens/screen";
import { Components } from "./views/Components/Components";

export const devViews = [
  {
    id: "dev.components.overview",
    title: "dev.components.overview.title",
    Component: Components,
  },
] as const satisfies readonly ViewComponent[];

export type DevViewId = (typeof devViews)[number]["id"];
