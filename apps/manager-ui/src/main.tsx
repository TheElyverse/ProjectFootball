import { StrictMode } from "react";
import { createRoot } from "react-dom/client";
import { Game } from "./Game";
import { I18nProvider } from "./i18n";
import { isUnrealHost } from "./ue/bridge";
import "./styles.css";

if (!isUnrealHost()) {
  document.documentElement.classList.add("browser-mock");
}

const root = document.getElementById("root");
if (root === null) {
  throw new Error("index.html has no #root element");
}
createRoot(root).render(
  <StrictMode>
    <I18nProvider>
      <div className="h-full bg-transparent text-slate-200">
        <Game />
      </div>
    </I18nProvider>
  </StrictMode>,
);
