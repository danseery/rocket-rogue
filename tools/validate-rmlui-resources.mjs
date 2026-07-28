import {
  existsSync,
  readFileSync,
  readdirSync,
  statSync
} from "node:fs";
import { dirname, relative, resolve, sep } from "node:path";
import { fileURLToPath } from "node:url";

export const requiredTemplateNames = [
  "rr-document-shell",
  "rr-workspace-shell",
  "rr-control-shell",
  "rr-surface-minigame-shell",
  "rr-mining-shell",
  "rr-takeover-shell",
  "rr-results-shell",
  "rr-modal-shell"
];

export const requiredStyleLayers = [
  "tokens.rcss",
  "primitives.rcss",
  "shells.rcss",
  "families.rcss",
  "screen-exceptions.rcss",
  "legacy.rcss"
];

const requiredDocumentHosts = [
  "rr-document",
  "rr-scene-overlay-host",
  "rr-panel",
  "rr-modal-host",
  "rr-controller-prompt-host",
  "rr-performance-host"
];

function normalizePath(path) {
  return path.split(sep).join("/");
}

function collectFiles(root, directory = root) {
  const files = [];
  for (const entry of readdirSync(directory)) {
    const path = resolve(directory, entry);
    if (statSync(path).isDirectory()) {
      files.push(...collectFiles(root, path));
    } else if (path.endsWith(".rml") || path.endsWith(".rcss")) {
      files.push(path);
    }
  }
  return files.sort();
}

function parseAttributes(tag) {
  const attributes = new Map();
  const expression = /([:\w-]+)\s*=\s*(["'])(.*?)\2/gs;
  for (const match of tag.matchAll(expression)) {
    attributes.set(match[1].toLowerCase(), match[3]);
  }
  return attributes;
}

function stripMarkupComments(source) {
  return source.replace(/<!--[\s\S]*?-->/g, "");
}

function validateMarkupBalance(source, file, errors) {
  const cleaned = stripMarkupComments(source);
  const stack = [];
  const voidElements = new Set(["br", "hr", "img", "input", "link", "meta"]);
  const tagExpression = /<\s*(\/?)\s*([a-zA-Z][\w:-]*)\b[^>]*>/g;
  for (const match of cleaned.matchAll(tagExpression)) {
    const closing = match[1] === "/";
    const name = match[2].toLowerCase();
    const selfClosing = /\/\s*>$/.test(match[0]) || voidElements.has(name);
    if (closing) {
      const expected = stack.pop();
      if (expected !== name) {
        errors.push(`${file}: closing </${name}> does not match <${expected ?? "none"}>.`);
        return;
      }
    } else if (!selfClosing) {
      stack.push(name);
    }
  }
  if (stack.length > 0) {
    errors.push(`${file}: unclosed <${stack.at(-1)}> element.`);
  }
}

function validateStyleBalance(source, file, errors) {
  const cleaned = source.replace(/\/\*[\s\S]*?\*\//g, "");
  let depth = 0;
  let quote = "";
  for (let index = 0; index < cleaned.length; index += 1) {
    const character = cleaned[index];
    if (quote) {
      if (character === quote && cleaned[index - 1] !== "\\") {
        quote = "";
      }
      continue;
    }
    if (character === "'" || character === "\"") {
      quote = character;
    } else if (character === "{") {
      depth += 1;
    } else if (character === "}") {
      depth -= 1;
      if (depth < 0) {
        errors.push(`${file}: unexpected closing RCSS brace.`);
        return;
      }
    }
  }
  if (quote) {
    errors.push(`${file}: unterminated RCSS string.`);
  }
  if (depth !== 0) {
    errors.push(`${file}: unbalanced RCSS braces.`);
  }
}

function resolveDependency(root, sourcePath, href, errors) {
  const target = resolve(dirname(sourcePath), href);
  const relativeTarget = relative(root, target);
  if (relativeTarget.startsWith("..") || resolve(root, relativeTarget) !== target) {
    errors.push(`${normalizePath(relative(root, sourcePath))}: resource escapes assets/ui: ${href}`);
    return undefined;
  }
  if (!existsSync(target)) {
    errors.push(`${normalizePath(relative(root, sourcePath))}: missing linked resource: ${href}`);
    return undefined;
  }
  return target;
}

function parseBundle(source) {
  const match = source.match(/^\/\*\s*rr-bundle:\s*\r?\n([\s\S]*?)\*\/\s*\r?\n/);
  if (!match) {
    return undefined;
  }
  return match[1]
    .split(/\r?\n/)
    .map((line) => line.trim())
    .filter(Boolean);
}

function expectedBundleSource(root, layerNames) {
  const header = `/* rr-bundle:\n${layerNames.join("\n")}\n*/\n\n`;
  const layers = layerNames.map((name) =>
    readFileSync(resolve(root, "styles", name), "utf8")
      .replace(/\r\n/g, "\n")
      .trimEnd()
  );
  return `${header}${layers.join("\n\n")}\n`;
}

function findCycles(graph, root, errors) {
  const state = new Map();
  const stack = [];

  function visit(file) {
    state.set(file, 1);
    stack.push(file);
    for (const dependency of graph.get(file) ?? []) {
      if (state.get(dependency) === 1) {
        const start = stack.indexOf(dependency);
        const cycle = [...stack.slice(start), dependency]
          .map((path) => normalizePath(relative(root, path)))
          .join(" -> ");
        errors.push(`Resource dependency cycle: ${cycle}`);
      } else if (!state.has(dependency)) {
        visit(dependency);
      }
    }
    stack.pop();
    state.set(file, 2);
  }

  for (const file of graph.keys()) {
    if (!state.has(file)) {
      visit(file);
    }
  }
}

export function validateResourceGraph(resourceRoot = resolve("assets", "ui")) {
  const root = resolve(resourceRoot);
  const errors = [];
  if (!existsSync(root) || !statSync(root).isDirectory()) {
    return [`RmlUi resource root is missing: ${root}`];
  }

  const panelPath = resolve(root, "panel.rml");
  if (!existsSync(panelPath)) {
    return [`Required RmlUi document is missing: ${panelPath}`];
  }

  const files = collectFiles(root);
  const sources = new Map(files.map((file) => [
    file,
    readFileSync(file, "utf8").replace(/\r\n/g, "\n")
  ]));
  const graph = new Map(files.map((file) => [file, new Set()]));
  const templateDefinitions = new Map();
  const pendingTemplateReferences = [];
  const linkedTemplateFiles = [];

  for (const [filePath, source] of sources) {
    const file = normalizePath(relative(root, filePath));
    if (filePath.endsWith(".rcss")) {
      validateStyleBalance(source, file, errors);
      continue;
    }

    const markupSource = stripMarkupComments(source);
    validateMarkupBalance(markupSource, file, errors);
    for (const match of markupSource.matchAll(/<template\b[^>]*>/gi)) {
      const attributes = parseAttributes(match[0]);
      if (attributes.has("data-modal") || attributes.has("data-auto-modal")) {
        errors.push(`${file}: obsolete template[data-modal] transport is not allowed.`);
      }
      if (attributes.has("src")) {
        pendingTemplateReferences.push({ filePath, name: attributes.get("src") });
      }
    }

    const rootDefinition = markupSource.match(/^\s*<template\b[^>]*>/i);
    if (rootDefinition) {
      const attributes = parseAttributes(rootDefinition[0]);
      const name = attributes.get("name");
      const content = attributes.get("content");
      if (!name) {
        errors.push(`${file}: template definition is missing name.`);
      } else if (!name.startsWith("rr-")) {
        errors.push(`${file}: template name must use the rr- prefix: ${name}`);
      } else if (templateDefinitions.has(name)) {
        errors.push(`${file}: duplicate template name '${name}' also defined by ${normalizePath(relative(root, templateDefinitions.get(name)))}.`);
      } else {
        templateDefinitions.set(name, filePath);
      }
      if (!content) {
        errors.push(`${file}: template definition is missing its content target.`);
      } else {
        const matches = [...markupSource.matchAll(/\bid\s*=\s*(["'])(.*?)\1/gi)]
          .filter((match) => match[2] === content);
        if (matches.length !== 1) {
          errors.push(`${file}: content target '#${content}' must exist exactly once; found ${matches.length}.`);
        }
      }
    } else if (!markupSource.match(/^\s*<rml\b/i)) {
      errors.push(`${file}: resource must begin with <rml> or a template definition.`);
    }

    for (const match of markupSource.matchAll(/<body\b[^>]*>/gi)) {
      const template = parseAttributes(match[0]).get("template");
      if (template) {
        pendingTemplateReferences.push({ filePath, name: template });
      }
    }

    for (const match of markupSource.matchAll(/<link\b[^>]*>/gi)) {
      const attributes = parseAttributes(match[0]);
      const type = attributes.get("type");
      const href = attributes.get("href");
      if (type !== "text/template" && type !== "text/rcss") {
        continue;
      }
      if (!href) {
        errors.push(`${file}: ${type} link is missing href.`);
        continue;
      }
      const dependency = resolveDependency(root, filePath, href, errors);
      if (dependency) {
        if (type === "text/template" && !href.endsWith(".rml")) {
          errors.push(`${file}: template link must target an .rml resource: ${href}`);
        } else if (type === "text/rcss" && !href.endsWith(".rcss")) {
          errors.push(`${file}: stylesheet link must target an .rcss resource: ${href}`);
        }
        if (!graph.has(dependency)) {
          errors.push(`${file}: linked resource is not an RML/RCSS graph member: ${href}`);
        } else {
          graph.get(filePath).add(dependency);
        }
        if (type === "text/template") {
          linkedTemplateFiles.push({ file, dependency, href });
        }
      }
    }
  }

  const templateDefinitionFiles = new Set(templateDefinitions.values());
  for (const link of linkedTemplateFiles) {
    if (!templateDefinitionFiles.has(link.dependency)) {
      errors.push(`${link.file}: linked template does not define a template root: ${link.href}`);
    }
  }

  for (const reference of pendingTemplateReferences) {
    const dependency = templateDefinitions.get(reference.name);
    if (!dependency) {
      errors.push(`${normalizePath(relative(root, reference.filePath))}: unknown template '${reference.name}'.`);
    } else {
      graph.get(reference.filePath).add(dependency);
    }
  }

  for (const name of requiredTemplateNames) {
    if (!templateDefinitions.has(name)) {
      errors.push(`Required template is missing: ${name}`);
    }
  }

  const documentTemplate = templateDefinitions.get("rr-document-shell");
  if (documentTemplate) {
    const documentSource = stripMarkupComments(sources.get(documentTemplate));
    for (const host of requiredDocumentHosts) {
      const matches = [...documentSource.matchAll(
        new RegExp(`\\bid\\s*=\\s*["']${host}["']`, "gi"),
      )];
      if (matches.length !== 1) {
        errors.push(
          `templates/rr-document-shell.rml: stable host '#${host}' must exist exactly once; found ${matches.length}.`,
        );
      }
    }
  }

  const allStylePath = resolve(root, "styles", "all.rcss");
  if (!sources.has(allStylePath)) {
    errors.push("Required consolidated stylesheet is missing: styles/all.rcss");
  } else {
    const bundleLayers = parseBundle(sources.get(allStylePath));
    if (!bundleLayers) {
      errors.push("styles/all.rcss: missing rr-bundle source manifest.");
    } else {
      if (bundleLayers.join("\n") !== requiredStyleLayers.join("\n")) {
        errors.push(`styles/all.rcss: rr-bundle layers must be exactly ${requiredStyleLayers.join(", ")}.`);
      }
      const dependencies = [];
      for (const name of bundleLayers) {
        const dependency = resolveDependency(root, allStylePath, name, errors);
        if (dependency) {
          graph.get(allStylePath).add(dependency);
          dependencies.push(dependency);
        }
      }
      if (dependencies.length === bundleLayers.length) {
        const expected = expectedBundleSource(root, bundleLayers);
        if (sources.get(allStylePath) !== expected) {
          errors.push("styles/all.rcss: consolidated stylesheet is out of sync with its authored layers.");
        }
      }
    }
  }

  const panelSource = stripMarkupComments(sources.get(panelPath));
  const panelHead = panelSource.match(/<head\b[^>]*>([\s\S]*?)<\/head>/i);
  const panelHeadTemplateFiles = new Set();
  if (!panelHead) {
    errors.push("panel.rml: document head is missing.");
  } else {
    for (const match of panelHead[1].matchAll(/<link\b[^>]*>/gi)) {
      const attributes = parseAttributes(match[0]);
      if (attributes.get("type") !== "text/template" || !attributes.get("href")) {
        continue;
      }
      const dependency = resolve(dirname(panelPath), attributes.get("href"));
      if (sources.has(dependency)) {
        panelHeadTemplateFiles.add(dependency);
      }
    }
  }
  for (const [name, templatePath] of templateDefinitions) {
    if (!panelHeadTemplateFiles.has(templatePath)) {
      errors.push(`panel.rml: template '${name}' is not linked directly from the document head.`);
    }
  }
  if (!(graph.get(panelPath) ?? new Set()).has(allStylePath)) {
    errors.push("panel.rml: styles/all.rcss is not linked from the document head.");
  }
  if (!/<body\b[^>]*\btemplate\s*=\s*(["'])rr-document-shell\1/i.test(panelSource)) {
    errors.push("panel.rml: body must instantiate rr-document-shell.");
  }

  findCycles(graph, root, errors);

  const reachable = new Set();
  function markReachable(file) {
    if (reachable.has(file)) {
      return;
    }
    reachable.add(file);
    for (const dependency of graph.get(file) ?? []) {
      markReachable(dependency);
    }
  }
  markReachable(panelPath);
  for (const file of files) {
    if (!reachable.has(file)) {
      errors.push(`${normalizePath(relative(root, file))}: resource is not reachable from panel.rml.`);
    }
  }

  return [...new Set(errors)];
}

function runCli() {
  const root = process.argv[2] ? resolve(process.argv[2]) : resolve("assets", "ui");
  const errors = validateResourceGraph(root);
  if (errors.length > 0) {
    for (const error of errors) {
      console.error(error);
    }
    process.exitCode = 1;
    return;
  }
  console.log(`Validated RmlUi resource graph: ${root}`);
}

if (process.argv[1] && resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  runCli();
}
