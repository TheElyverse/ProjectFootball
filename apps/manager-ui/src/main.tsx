import { QueryClient, QueryClientProvider } from "@tanstack/react-query";
import { StrictMode } from "react";
import { createRoot } from "react-dom/client";
import { Game } from "./Game";
import { I18nProvider } from "./i18n";
import { isUnrealHost } from "./ue/bridge";
import "./styles.css";

if (!isUnrealHost()) {
  document.documentElement.classList.add("browser-mock");
}

// Game data changes only when the game says so, so cached query results never go stale
// on their own, and a failed query is not retried.
const queryClient = new QueryClient({
  defaultOptions: {
    queries: {
      staleTime: Infinity,
      retry: false,
      refetchOnWindowFocus: false,
      refetchOnReconnect: false,
    },
  },
});

const root = document.getElementById("root");
if (root === null) {
  throw new Error("index.html has no #root element");
}
createRoot(root).render(
  <StrictMode>
    <QueryClientProvider client={queryClient}>
      <I18nProvider>
        <div className="h-full bg-transparent text-slate-200">
          <Game />
        </div>
      </I18nProvider>
    </QueryClientProvider>
  </StrictMode>,
);
