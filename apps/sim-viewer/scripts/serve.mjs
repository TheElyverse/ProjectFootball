// Serves the viewer on http://localhost:<port>/ with plain node:http: browsers
// do not load ES modules from file:// URLs. With a frame file,
//   pnpm run serve frames.json
// (relative to the directory pnpm was started in) it is served at
// /frames.json and the printed URL opens it directly.
// Local debugging only: it binds to 127.0.0.1 and serves this directory.

import { createServer } from "node:http";
import { readFile } from "node:fs/promises";
import { extname, join, normalize, resolve, sep } from "node:path";
import { fileURLToPath } from "node:url";

const root = resolve(fileURLToPath(new URL("..", import.meta.url)));
const args = process.argv.slice(2);
const portIndex = args.indexOf("--port");
const port = portIndex >= 0 ? Number(args.splice(portIndex, 2)[1]) : 8080;
// pnpm runs scripts in the package directory and passes the caller's in INIT_CWD.
const callerDirectory = process.env.INIT_CWD ?? process.cwd();
const framesPath = args[0] === undefined ? undefined : resolve(callerDirectory, args[0]);

const types = {
  ".html": "text/html; charset=utf-8",
  ".js": "text/javascript; charset=utf-8",
  ".css": "text/css; charset=utf-8",
  ".json": "application/json",
};

// The request path, decoded; undefined for a malformed percent-encoding such as
// "/%E0%A4%A", which decodeURIComponent() rejects with an exception -- thrown
// inside the async handler it would end the whole server.
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
  let file;
  if (path === "/frames.json" && framesPath !== undefined) {
    file = framesPath;
  } else {
    file = normalize(join(root, path === "/" ? "index.html" : path));
    if (file !== root && !file.startsWith(root + sep)) {
      response.writeHead(403).end();
      return;
    }
  }
  try {
    const body = await readFile(file);
    response.writeHead(200, {
      "Content-Type": types[extname(file)] ?? "application/octet-stream",
      "Cache-Control": "no-store",
    });
    response.end(body);
  } catch {
    response.writeHead(404).end("not found");
  }
});

server.listen(port, "127.0.0.1", () => {
  const query = framesPath === undefined ? "" : "?frames=frames.json";
  console.log(`sim-viewer: http://localhost:${port}/${query}`);
});
