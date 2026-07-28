import assert from "node:assert/strict";
import {
  cpSync,
  mkdtempSync,
  readFileSync,
  rmSync,
  unlinkSync,
  writeFileSync
} from "node:fs";
import { dirname, join, resolve } from "node:path";
import { tmpdir } from "node:os";
import { fileURLToPath } from "node:url";
import { validateResourceGraph } from "./validate-rmlui-resources.mjs";

const repositoryRoot = resolve(dirname(fileURLToPath(import.meta.url)), "..");
const sourceRoot = resolve(repositoryRoot, "assets", "ui");
const temporaryRoot = mkdtempSync(join(tmpdir(), "orebit-rmlui-resources-"));

function fixture(name) {
  const root = join(temporaryRoot, name);
  cpSync(sourceRoot, root, { recursive: true });
  return root;
}

function replace(path, search, replacement) {
  const source = readFileSync(path, "utf8");
  assert(source.includes(search), `Fixture token not found in ${path}: ${search}`);
  writeFileSync(path, source.replace(search, replacement));
}

function expectError(root, pattern) {
  const errors = validateResourceGraph(root);
  assert(
    errors.some((error) => pattern.test(error)),
    `Expected ${pattern}, received:\n${errors.join("\n")}`
  );
}

try {
  assert.deepEqual(validateResourceGraph(sourceRoot), []);
  {
    const shellStyles = readFileSync(join(sourceRoot, "styles", "shells.rcss"), "utf8");
    assert(
      !shellStyles.includes("#rr-panel > .rr-shell > .rr-shell-lane"),
      "template shell lanes must not be neutralized unconditionally"
    );
    assert(
      shellStyles.includes(
        "#rr-panel > .rr-shell.rr-legacy-content-owns-lane > .rr-shell-lane"
      ),
      "legacy content-owned lane compatibility must require its explicit shell class"
    );
  }

  {
    const root = fixture("missing-link");
    unlinkSync(join(root, "templates", "rr-results-shell.rml"));
    expectError(root, /missing linked resource|Required template is missing/);
  }

  {
    const root = fixture("missing-head-link");
    replace(
      join(root, "panel.rml"),
      '\t<link type="text/template" href="templates/rr-results-shell.rml"/>\n',
      ""
    );
    expectError(root, /template 'rr-results-shell' is not linked directly from the document head/);
  }

  {
    const root = fixture("duplicate-name");
    replace(
      join(root, "templates", "rr-workspace-shell.rml"),
      'name="rr-workspace-shell"',
      'name="rr-document-shell"'
    );
    expectError(root, /duplicate template name/);
  }

  {
    const root = fixture("missing-content");
    replace(
      join(root, "templates", "rr-control-shell.rml"),
      'id="rr-control-content"',
      'id="rr-control-content-missing"'
    );
    expectError(root, /content target '#rr-control-content'/);
  }

  {
    const root = fixture("commented-host");
    replace(
      join(root, "templates", "rr-document-shell.rml"),
      '\t<div id="rr-performance-host" class="rr-performance-host"></div>',
      '\t<!-- <div id="rr-performance-host" class="rr-performance-host"></div> -->'
    );
    expectError(root, /stable host '#rr-performance-host' must exist exactly once; found 0/);
  }

  {
    const root = fixture("duplicate-host");
    replace(
      join(root, "templates", "rr-document-shell.rml"),
      '\t<div id="rr-performance-host" class="rr-performance-host"></div>',
      '\t<div id="rr-performance-host" class="rr-performance-host"></div>\n'
        + '\t<div id="rr-performance-host" class="rr-performance-host"></div>'
    );
    expectError(root, /stable host '#rr-performance-host' must exist exactly once; found 2/);
  }

  {
    const root = fixture("cycle");
    replace(
      join(root, "templates", "rr-document-shell.rml"),
      "<head>",
      '<head>\n\t<link type="text/template" href="../panel.rml"/>'
    );
    expectError(root, /Resource dependency cycle/);
  }

  {
    const root = fixture("obsolete-modal");
    replace(
      join(root, "templates", "rr-modal-shell.rml"),
      'name="rr-modal-shell"',
      'name="rr-modal-shell" data-modal="settings"'
    );
    expectError(root, /obsolete template\[data-modal\]/);
  }

  {
    const root = fixture("malformed-rml");
    replace(
      join(root, "templates", "rr-results-shell.rml"),
      "</section>",
      ""
    );
    expectError(root, /closing <\/body> does not match <section>|unclosed <section>/);
  }

  {
    const root = fixture("bundle-drift");
    const bundle = join(root, "styles", "all.rcss");
    writeFileSync(bundle, `${readFileSync(bundle, "utf8")}\n.rr-drift { color: red; }\n`);
    expectError(root, /consolidated stylesheet is out of sync/);
  }

  console.log("RmlUi resource graph validator tests passed.");
} finally {
  rmSync(temporaryRoot, { recursive: true, force: true });
}
