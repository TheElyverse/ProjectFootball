import { useEffect } from "react";
import { onGameEvent, type NavigationInput } from "@/ue/bridge";

const focusableSelector =
  "button:not([disabled]), a[href], [tabindex]:not([tabindex='-1'])";

// Moves the focus to the next (1) or previous (-1) focusable control in document order,
// stopping at the first and the last one. Without focus, it starts at the first or last.
export function moveFocus(step: 1 | -1) {
  const controls = [
    ...document.querySelectorAll<HTMLElement>(focusableSelector),
  ];
  const current = controls.findIndex(
    (control) => control === document.activeElement,
  );
  const next =
    current === -1
      ? step === 1
        ? 0
        : controls.length - 1
      : Math.min(Math.max(current + step, 0), controls.length - 1);
  controls[next]?.focus();
}

// The page owns the focus: the game only forwards navigation inputs. Up and down move the
// focus, confirm clicks the focused control.
export function useFocusInput() {
  useEffect(
    () =>
      onGameEvent("input", (input: NavigationInput) => {
        switch (input) {
          case "nav.up":
            moveFocus(-1);
            break;
          case "nav.down":
            moveFocus(1);
            break;
          case "nav.confirm":
            if (document.activeElement instanceof HTMLElement) {
              document.activeElement.click();
            }
            break;
        }
      }),
    [],
  );
}

// Calls onBack on the back input.
export function useBackInput(onBack: () => void) {
  useEffect(
    () =>
      onGameEvent("input", (input: NavigationInput) => {
        if (input === "nav.back") {
          onBack();
        }
      }),
    [onBack],
  );
}
