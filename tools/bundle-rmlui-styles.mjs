import { readFileSync, writeFileSync } from "node:fs";
import { resolve } from "node:path";

import { requiredStyleLayers } from "./validate-rmlui-resources.mjs";

const root = process.argv[2]
  ? resolve(process.argv[2])
  : resolve("assets", "ui");
const stylesRoot = resolve(root, "styles");
const header = `/* rr-bundle:\n${requiredStyleLayers.join("\n")}\n*/\n\n`;
const layers = requiredStyleLayers.map((name) =>
  readFileSync(resolve(stylesRoot, name), "utf8")
    .replace(/\r\n/g, "\n")
    .trimEnd()
);
const outputPath = resolve(stylesRoot, "all.rcss");

writeFileSync(outputPath, `${header}${layers.join("\n\n")}\n`, "utf8");
console.log(`Bundled ${requiredStyleLayers.length} RmlUi style layers: ${outputPath}`);
