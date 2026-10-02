import { command, isUnrealHost } from "../bridge";

const buttonClass =
  "w-64 rounded bg-slate-800 px-4 py-2 text-left hover:bg-slate-700 focus-visible:outline-2 focus-visible:outline-sky-400 disabled:text-slate-500 disabled:hover:bg-slate-800";

export function MainMenu({ onStartGame }: { onStartGame: () => void }) {
  return (
    <div className="flex h-full flex-col items-center justify-center gap-3">
      <h1 className="mb-6 text-3xl font-semibold text-slate-100">Elyverse: Football</h1>
      <button type="button" className={buttonClass} onClick={onStartGame} autoFocus>
        Start game
      </button>
      <button type="button" className={buttonClass} disabled>
        Load game
      </button>
      <button type="button" className={buttonClass} disabled>
        Settings
      </button>
      {isUnrealHost() && (
        <button type="button" className={buttonClass} onClick={() => void command("quit")}>
          Quit
        </button>
      )}
    </div>
  );
}
