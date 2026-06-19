import { createReadStream, existsSync, statSync } from "node:fs";
import { createServer } from "node:http";
import { extname, join, normalize, resolve, sep } from "node:path";

const root = resolve(process.argv[2] ?? "build/web-release");
const port = Number.parseInt(process.argv[3] ?? "8080", 10);

const mimeTypes = new Map([
  [".html", "text/html; charset=utf-8"],
  [".js", "text/javascript; charset=utf-8"],
  [".wasm", "application/wasm"],
  [".css", "text/css; charset=utf-8"],
  [".json", "application/json; charset=utf-8"],
  [".png", "image/png"],
  [".svg", "image/svg+xml"]
]);

function send(res, status, body) {
  res.writeHead(status, { "content-type": "text/plain; charset=utf-8" });
  res.end(body);
}

function insideRoot(path) {
  const relative = normalize(path).slice(root.length);
  return path === root || relative.startsWith(sep) || relative === "";
}

if (!existsSync(root)) {
  console.error(`Build directory not found: ${root}`);
  console.error("Build the web target first, then run this server again.");
  process.exit(1);
}

createServer((req, res) => {
  const url = new URL(req.url ?? "/", "http://localhost");
  let pathname = decodeURIComponent(url.pathname);
  if (pathname === "/") {
    pathname = "/rocket_rogue.html";
  }

  let filePath = resolve(join(root, pathname));
  if (!insideRoot(filePath)) {
    send(res, 403, "Forbidden");
    return;
  }

  if (existsSync(filePath) && statSync(filePath).isDirectory()) {
    filePath = join(filePath, "index.html");
  }

  if (!existsSync(filePath)) {
    send(res, 404, "Not found");
    return;
  }

  res.writeHead(200, {
    "content-type": mimeTypes.get(extname(filePath)) ?? "application/octet-stream",
    "cache-control": "no-store",
    "cross-origin-opener-policy": "same-origin",
    "cross-origin-embedder-policy": "require-corp"
  });
  createReadStream(filePath).pipe(res);
}).listen(port, () => {
  console.log(`Serving ${root}`);
  console.log(`Open http://localhost:${port}/rocket_rogue.html`);
});
