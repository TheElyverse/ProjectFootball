import tailwindcss from "@tailwindcss/vite";
import react from "@vitejs/plugin-react";
import { defineConfig } from "vite";
import { viteSingleFile } from "vite-plugin-singlefile";

// Unreal loads dist/index.html from a file:// URL, where Chromium refuses to fetch
// module scripts, so the build inlines all scripts and styles into that one file.
export default defineConfig({
  plugins: [react(), tailwindcss(), viteSingleFile()],
});
