import type { ReactNode } from "react";
import { isUnrealHost } from "../bridge";
import { useFramesPerSecond } from "../hooks/useFramesPerSecond";
import { managerScreens, type ManagerScreenId } from "../screens";

interface ManagerLayoutProps {
  activeScreen: ManagerScreenId;
  onNavigate: (screen: ManagerScreenId) => void;
  onMainMenu: () => void;
  children: ReactNode;
}

// The frame around every manager screen: navigation on top, the screen in the middle and a
// status line at the bottom.
export function ManagerLayout({ activeScreen, onNavigate, onMainMenu, children }: ManagerLayoutProps) {
  const framesPerSecond = useFramesPerSecond();

  return (
    <div className="flex h-full flex-col">
      <nav className="flex items-center gap-1 bg-slate-900 px-2 py-1">
        <button
          type="button"
          className="rounded px-3 py-1 text-slate-400 hover:bg-slate-800 focus-visible:outline-2 focus-visible:outline-sky-400"
          onClick={onMainMenu}
        >
          Menu
        </button>
        {managerScreens.map((screen) => (
          <button
            key={screen.id}
            type="button"
            className={`rounded px-3 py-1 hover:bg-slate-800 focus-visible:outline-2 focus-visible:outline-sky-400 ${
              screen.id === activeScreen ? "bg-slate-800 text-slate-100" : "text-slate-300"
            }`}
            onClick={() => onNavigate(screen.id)}
          >
            {screen.title}
          </button>
        ))}
      </nav>
      <main className="min-h-0 flex-1">{children}</main>
      <footer className="flex gap-6 bg-slate-900 px-3 py-0.5 font-mono text-xs text-emerald-300">
        <span>{isUnrealHost() ? "Unreal bridge" : "Browser mock"}</span>
        <span>{framesPerSecond} fps</span>
      </footer>
    </div>
  );
}
