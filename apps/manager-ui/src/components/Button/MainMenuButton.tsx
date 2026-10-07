import type { MouseEventHandler } from "react";

type Props = {
  text: string;
  onClick?: MouseEventHandler<HTMLButtonElement>;
  autoFocus?: boolean;
};

// Hovering focuses the button, so mouse and navigation inputs share one highlight.
export function MainMenuButton({ text, onClick, autoFocus }: Props) {
  return (
    <button
      type="button"
      autoFocus={autoFocus}
      className="border-l-4 border-transparent py-3 pl-6 text-left text-2xl font-semibold tracking-wide text-slate-300 uppercase outline-none focus:border-shiny-elyverse-blue active:border-amber-400 focus:bg-white/5 focus:text-white"
      onClick={onClick}
      onMouseEnter={(event) => event.currentTarget.focus()}
    >
      {text}
    </button>
  );
}
