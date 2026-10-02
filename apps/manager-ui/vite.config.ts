import tailwindcss from "@tailwindcss/vite";
import react from "@vitejs/plugin-react";
import { defineConfig } from "vite";
import { viteSingleFile } from "vite-plugin-singlefile";

// Unreal loads the page from a file:// URL, where Chromium refuses to fetch module
// scripts, so the build inlines all scripts and styles into one index.html. It goes
// straight into the Unreal project, which stages that folder as loose files.
export default defineConfig({
  plugins: [react(), tailwindcss(), viteSingleFile()],
  build: {
    outDir: "../unreal-game/Content/ManagerUI",
    emptyOutDir: true,
  },
});
