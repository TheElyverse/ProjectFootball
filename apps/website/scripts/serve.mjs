// Serves the built site (dist/) on http://localhost:<port>/ with plain
// node:http, to look at it locally after `pnpm run build`:
//   pnpm run serve
//   pnpm run serve --port 9000
// Local preview only: it binds to 127.0.0.1. The site itself is static and can
// be hosted by any web server or static hosting service.

import { createServer } from "node:http";
import { readFile } from "node:fs/promises";
import { extname, join, normalize, resolve, sep } from "node:path";
import { fileURLToPath } from "node:url";

const root = resolve(fileURLToPath(new URL("../dist", import.meta.url)));
const args = process.argv.slice(2);
const portIndex = args.indexOf("--port");
const port = portIndex >= 0 ? Number(args[portIndex + 1]) : 8081;

const types = {
  ".html": "text/html; charset=utf-8",
  ".js": "text/javascript; charset=utf-8",
  ".css": "text/css; charset=utf-8",
  ".svg": "image/svg+xml",
  ".png": "image/png",
  ".webp": "image/webp",
  ".ico": "image/x-icon",
  ".txt": "text/plain; charset=utf-8",
};

// The request path, decoded; undefined for a malformed percent-encoding, which
// decodeURIComponent() rejects with an exception.
function requestPath(request) {
  try {
    return decodeURIComponent(new URL(request.url ?? "/", "http://localhost").pathname);
  } catch {
    return undefined;
  }
}

const server = createServer(async (request, response) => {
  const path = requestPath(request);
  if (path === undefined) {
    response.writeHead(400).end("bad request");
    return;
  }
  const file = normalize(join(root, path.endsWith("/") ? `${path}index.html` : path));
  if (file !== root && !file.startsWith(root + sep)) {
    response.writeHead(403).end();
    return;
  }
  try {
    const body = await readFile(file);
    response.writeHead(200, {
      "Content-Type": types[extname(file)] ?? "application/octet-stream",
      "Cache-Control": "no-store",
    });
    response.end(body);
  } catch {
    response.writeHead(404).end("not found - did you run `pnpm run build`?");
  }
});

server.listen(port, "127.0.0.1", () => {
  console.log(`website: http://localhost:${port}/`);
});
