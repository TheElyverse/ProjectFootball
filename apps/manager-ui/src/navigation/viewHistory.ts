import { useCallback, useState } from "react";
import { useBackInput } from "./focus";

// The views a screen has shown, starting at initialView. Opening a view adds it, and the
// back input returns to the previous one; on the first view, back does nothing.
export function useViewHistory<View>(initialView: View) {
  const [history, setHistory] = useState<readonly View[]>([initialView]);

  const open = useCallback((view: View) => {
    setHistory((views) => (views.at(-1) === view ? views : [...views, view]));
  }, []);
  const back = useCallback(() => {
    setHistory((views) => (views.length > 1 ? views.slice(0, -1) : views));
  }, []);
  useBackInput(back);

  return { view: history.at(-1) ?? initialView, open };
}
