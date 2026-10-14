import { MouseEventHandler, ReactNode } from "react";
import { clsx } from "clsx";

type Props = {
  text: string;
  onClick?: MouseEventHandler<HTMLButtonElement>;
  autoFocus?: boolean;
  /**
   * @default 1
   */
  level?: 1 | 2;
  hint?: ReactNode;
};

// Hovering focuses the button, so mouse and navigation inputs share one highlight.
export function MainMenuButton({
  text,
  onClick,
  autoFocus,
  level = 1,
  hint,
}: Props) {
  return (
    <button
      type="button"
      autoFocus={autoFocus}
      className={clsx(
        "flex flex-col border-l-4 border-transparent pt-2 pb-3 pl-8 text-left text-slate-300 outline-none",
        "focus:border-volt focus:bg-linear-to-r focus:from-volt/20 focus:to-transparent",
        "active:border-volt active:text-volt",
      )}
      onClick={onClick}
      onMouseEnter={(event) => event.currentTarget.focus()}
    >
      <div
        className={clsx(
          "font-label font-bold uppercase",
          level === 1 ? "text-5xl italic" : "text-3xl",
        )}
      >
        {text}
      </div>
      {hint && <div className="text-slate-400">{hint}</div>}
    </button>
  );
}
