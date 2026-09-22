// Isolated, synthetic browser QA. Never uses the campaign's origin or data.
// Run with node tools/web-save-browser-check.mjs, then open the printed URL.
import { createServer } from 'node:http';
import { readFileSync } from 'node:fs';
const shell = readFileSync(new URL('../web/shell.html', import.meta.url), 'utf8');
const presentation = shell.slice(shell.indexOf('    function createSaveStorage('), shell.indexOf('    window.RocketBridge ='))
  .replace('createSaveStorage(window.indexedDB,', 'createSaveStorage(testIndexedDB,');
const html = `<!doctype html><meta charset="utf-8"><title>Isolated save QA</title>
<style>body{background:#07131c;color:#c9e8ef;font:18px sans-serif;padding:32px}button{padding:12px;margin:8px}</style>
<h1>Isolated save-storage check</h1><p>Synthetic test data only; no campaign data.</p>
<button id="run">Run real IndexedDB checks</button><button id="fail">Simulate failed dock save</button>
<button id="enable">Restore storage (then Retry save)</button><pre id="result">Ready</pre>
<button id="milestone">Preview milestone save icon</button>
<script>
let failWrites = false;
const testIndexedDB = { open(...args) {
  const request = indexedDB.open(...args), proxy = {};
  for (const event of ['onupgradeneeded','onblocked','onerror','onsuccess']) {
    request[event] = () => {
      proxy.error = request.error;
      proxy.result = request.result;
      if (event === 'onsuccess') {
        const db = request.result;
        proxy.result = { close: () => db.close(),
          set onversionchange(value) { db.onversionchange = value; },
          transaction(name, mode, options) {
            if (mode === 'readwrite' && failWrites) throw new DOMException('Injected disk full', 'QuotaExceededError');
            return db.transaction(name, mode, options);
          }
        };
      }
      proxy[event]?.();
    };
  }
  return proxy;
} };
${presentation}
const output = document.querySelector('#result');
const check = (condition, label) => { if (!condition) throw new Error(label); output.textContent += '\\nPASS ' + label; };
document.querySelector('#run').onclick = async () => {
  try {
    output.textContent = 'Running';
    await saveStorage.initialize();
    const large = 'terrain='.repeat(1750000) + 'dock';
    saveStorage.store('save', large);
    check(saveStorage.unsafeToLeave(), 'pending write protects navigation');
    await saveStorage.flush();
    check(!saveStorage.unsafeToLeave(), '14 MB transaction committed');
    const restored = createSaveStorage(testIndexedDB, () => '', () => {});
    await restored.initialize();
    check(restored.load('save') === large, 'fresh loader restores exact large snapshot');
    failWrites = true;
    saveStorage.store('save', 'synthetic dock arrival'); await saveStorage.flush();
    check(saveStorage.unsafeToLeave(), 'failed write remains pending');
    await restored.initialize();
    check(restored.load('save') === large, 'failed write preserves committed save');
    check(saveStorage.load('save') === 'synthetic dock arrival', 'failed snapshot retained for recovery export');
    failWrites = false; await saveStorage.flush();
    await restored.initialize();
    check(restored.load('save') === 'synthetic dock arrival', 'retry persists newest snapshot');
    output.textContent += '\\nALL CHECKS PASSED';
  } catch (error) { failWrites = false; output.textContent += '\\nFAIL ' + error.stack; }
};
document.querySelector('#fail').onclick = async () => {
  await saveStorage.initialize(); failWrites = true;
  saveStorage.store('save', 'synthetic failed arrival');
};
document.querySelector('#enable').onclick = () => { failWrites = false; };
document.querySelector('#milestone').onclick = async () => {
  await saveStorage.initialize();
  saveStorage.store('save', 'synthetic orbit capture', true);
};
</script>`;
const server = createServer((request, response) => {
  if (request.url === '/assets/ui/save-icon.png') {
    response.writeHead(200, { 'Content-Type': 'image/png' });
    response.end(readFileSync(new URL('../assets/ui/save-icon.png', import.meta.url)));
    return;
  }
  response.writeHead(200, { 'Content-Type': 'text/html; charset=utf-8', 'Cache-Control': 'no-store' });
  response.end(html);
});
server.listen(0, '127.0.0.1', () => console.log(`Save QA: http://127.0.0.1:${server.address().port}/`));
