import type { ReactNode } from "react";
import { FormattedMessage } from "react-intl";
import { isUnrealHost } from "@/ue/bridge";
import { useFramesPerSecond } from "@/hooks/useFramesPerSecond";
import { managerViews, type ManagerViewId } from "./views";

interface ManagerLayoutProps {
  activeView: ManagerViewId;
  onNavigate: (view: ManagerViewId) => void;
  onMainMenu: () => void;
  children: ReactNode;
}

// The frame of the manager screen: navigation on top, the active view in the middle and a
// status line at the bottom.
export function ManagerLayout({
  activeView,
  onNavigate,
  onMainMenu,
  children,
}: ManagerLayoutProps) {
  const framesPerSecond = useFramesPerSecond();

  return (
    <div className="flex h-full flex-col bg-default-bg">
      <nav className="flex items-center gap-1 bg-slate-900 px-2 py-1">
        <button
          type="button"
          className="rounded px-3 py-1 text-slate-400 hover:bg-slate-800 focus-visible:outline-2 focus-visible:outline-sky-400"
          onClick={onMainMenu}
        >
          <FormattedMessage id="nav.menu" />
        </button>
        {managerViews.map((view) => (
          <button
            key={view.id}
            type="button"
            className={`rounded px-3 py-1 hover:bg-slate-800 focus-visible:outline-2 focus-visible:outline-sky-400 ${
              view.id === activeView
                ? "bg-slate-800 text-slate-100"
                : "text-slate-300"
            }`}
            onClick={() => onNavigate(view.id)}
          >
            <FormattedMessage id={view.title} />
          </button>
        ))}
      </nav>
      <main className="min-h-0 flex-1">{children}</main>
      <footer className="flex gap-6 bg-slate-900 px-3 py-0.5 font-mono text-xs text-emerald-300">
        <span>
          <FormattedMessage
            id={isUnrealHost() ? "status.unrealBridge" : "status.browserMock"}
          />
        </span>
        <span>
          <FormattedMessage
            id="status.framesPerSecond"
            values={{ framesPerSecond }}
          />
        </span>
      </footer>
    </div>
  );
}
