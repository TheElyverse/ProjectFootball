// Copies the static files of the site (HTML, JavaScript, images) from src/ to
// dist/. The stylesheet is not copied: the Tailwind CLI compiles
// src/styles.css into dist/styles.css in the second half of `pnpm run build`.

import { cpSync, rmSync } from "node:fs";
import { basename } from "node:path";
import { fileURLToPath } from "node:url";

const source = fileURLToPath(new URL("../src", import.meta.url));
const target = fileURLToPath(new URL("../dist", import.meta.url));

rmSync(target, { recursive: true, force: true });
cpSync(source, target, {
  recursive: true,
  filter: (path) => basename(path) !== "styles.css",
});

console.log("website: copied static files to dist/");
