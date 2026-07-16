import fs from "node:fs";
import path from "node:path";

const root = path.resolve(import.meta.dirname, "..");
const sourceRoot = path.join(root, "src");
const allowed = path.join(sourceRoot, "platform", "web") + path.sep;
const forbidden = [
  /#\s*include\s*[<"]emscripten(?:\/|\.)/,
  /\bEM_JS\s*\(/,
  /\bEM_ASM\b/,
  /\bemscripten_[a-zA-Z0-9_]+/,
  /\bwindow\.(?:localStorage|RocketBridge|RocketDesktop)/,
  /\bdocument\.(?:body|getElementById|visibilityState)/,
];

function files(directory) {
  return fs.readdirSync(directory, {withFileTypes: true}).flatMap((entry) => {
    const value = path.join(directory, entry.name);
    return entry.isDirectory() ? files(value) : [value];
  });
}

const violations = [];
for (const file of files(sourceRoot)) {
  if (!/\.(?:cpp|h)$/.test(file) || file.startsWith(allowed)) continue;
  const text = fs.readFileSync(file, "utf8");
  forbidden.forEach((pattern) => {
    if (pattern.test(text)) violations.push(`${path.relative(root, file)} matches ${pattern}`);
  });
}

if (violations.length) {
  console.error("Browser/Emscripten symbols escaped the web adapter boundary:\n" + violations.join("\n"));
  process.exit(1);
}

console.log("Platform boundary check passed.");
