import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import test from 'node:test';

const shell = readFileSync(new URL('../web/shell.html', import.meta.url), 'utf8');
const source = shell.slice(shell.indexOf('    function createSaveStorage('), shell.indexOf('    // A quiet confirmation'));
const create = new Function(`${source}; return createSaveStorage;`)();

// Small asynchronous IDB model: requests run in order, transactions commit only
// after request callbacks finish, and abort discards all writes (including backup).
function database() {
  const rows = new Map();
  const faults = { write: false, read: false, mismatch: false, open: false };
  const db = {
    createObjectStore() {}, close() {},
    transaction(_, mode) {
      const draft = new Map(rows), queue = [];
      let aborted = false;
      const tx = {
        error: null,
        abort() { aborted = true; },
        objectStore() {
          return {
            get(key) {
              const request = {};
              queue.push(() => {
                if (faults.read) throw new Error('Read unavailable');
                request.result = draft.get(key);
                if (faults.mismatch && mode === 'readwrite' && draft.get(key) !== rows.get(key)) request.result = 'corrupt';
                request.onsuccess?.();
              });
              return request;
            },
            put(value, key) { queue.push(() => {
              if (faults.write) { const error = new Error('Disk full'); error.name = 'QuotaExceededError'; throw error; }
              draft.set(key, value);
            }); }
          };
        }
      };
      setImmediate(() => {
        try { while (queue.length && !aborted) queue.shift()(); }
        catch (error) { tx.error = error; aborted = true; }
        if (aborted) tx.onabort?.();
        else {
          if (mode === 'readwrite') { rows.clear(); for (const [k, v] of draft) rows.set(k, v); }
          tx.oncomplete?.();
        }
      });
      return tx;
    }
  };
  return { rows, faults, indexedDB: { open() {
    const request = { result: db };
    setImmediate(() => {
      if (faults.open) { request.error = new Error('Storage unavailable'); request.onerror(); }
      else request.onsuccess();
    });
    return request;
  } } };
}
function instance(db, legacy = {}) {
  const notices = [];
  const store = create(db.indexedDB, key => legacy[key], state => notices.push(state));
  return { store, notices };
}

test('imports both legacy slots without deleting them; tombstone prevents resurrection', async () => {
  const db = database();
  const legacy = { rocket_rogue_save_v1: 'Titan', rocket_rogue_checkpoint_v21: 'Earth' };
  const { store } = instance(db, legacy);
  await store.initialize();
  assert.equal(store.load('save'), 'Titan');
  assert.equal(store.load('checkpoint'), 'Earth');
  store.clear('save'); await store.flush();
  const reload = instance(db, legacy).store; await reload.initialize();
  assert.equal(reload.load('save'), '');
  assert.equal(legacy.rocket_rogue_save_v1, 'Titan');
});

test('large campaign, dock and reveal snapshots survive committed reload', async () => {
  const db = database(), { store } = instance(db);
  await store.initialize();
  const terrain = 'visited-site-terrain='.repeat(700000);
  store.store('save', terrain + 'Titan');
  store.store('save', terrain + 'Earth dock');
  store.store('checkpoint', terrain + 'Earth dock');
  store.store('save', terrain + 'Straylight reveal');
  assert.equal(store.unsafeToLeave(), true);
  await store.flush();
  assert.equal(store.unsafeToLeave(), false);
  const reload = instance(db).store; await reload.initialize();
  assert.equal(reload.load('save'), terrain + 'Straylight reveal');
  assert.equal(reload.load('checkpoint'), terrain + 'Earth dock');
});

test('quota failure preserves durable progress and recovery snapshot; retry commits latest', async () => {
  const db = database(), { store, notices } = instance(db);
  await store.initialize();
  store.store('save', 'Titan'); await store.flush();
  db.faults.write = true;
  store.store('save', 'Earth dock'); await store.flush();
  assert.equal(db.rows.get('save'), 'Titan');
  assert.equal(store.load('save'), 'Earth dock');
  assert.equal(store.unsafeToLeave(), true);
  assert.match(notices.at(-1).error, /QuotaExceededError/);
  store.store('save', 'Straylight'); await store.flush();
  db.faults.write = false; await store.flush();
  assert.equal(db.rows.get('save'), 'Straylight');
  assert.equal(db.rows.get('save-previous'), 'Titan');
  assert.equal(notices.at(-1).error, '');
});

test('verification failure aborts transaction and checkpoint failures remain visible', async () => {
  const db = database(), { store, notices } = instance(db);
  await store.initialize();
  store.store('save', 'good'); await store.flush();
  db.faults.mismatch = true;
  store.store('save', 'new'); await store.flush();
  assert.equal(db.rows.get('save'), 'good');
  assert.match(store.error(), /verification/);
  db.faults.mismatch = false; await store.flush();
  db.faults.write = true;
  store.store('checkpoint', 'new checkpoint'); await store.flush();
  assert.equal(db.rows.get('save'), 'new');
  assert.equal(notices.at(-1).pending, 1);
  assert.match(store.error(), /QuotaExceededError/);
});

test('failed load blocks staging; failed migration preserves legacy; retry works', async () => {
  const db = database(); db.faults.open = true;
  const { store } = instance(db, { rocket_rogue_save_v1: 'Titan' });
  await assert.rejects(store.initialize());
  assert.throws(() => store.store('save', 'blank'), /loading/);
  db.faults.open = false; db.faults.write = true;
  await assert.rejects(store.initialize());
  assert.equal(store.load('save'), 'Titan');
  assert.equal(db.rows.has('save'), false);
  db.faults.write = false; await store.initialize();
  assert.equal(store.load('save'), 'Titan');
});

test('another tab cannot silently overwrite newer durable progress', async () => {
  const db = database(), first = instance(db).store;
  await first.initialize();
  const second = instance(db).store; await second.initialize();
  first.store('save', 'newer'); await first.flush();
  second.store('save', 'stale'); await second.flush();
  assert.equal(db.rows.get('save'), 'newer');
  assert.match(second.error(), /Another game tab/);
  assert.equal(second.load('save'), 'stale'); // Still exportable, never discarded.
});

test('existing IndexedDB saves do not depend on legacy storage access', async () => {
  const db = database();
  db.rows.set('save', 'new progress'); db.rows.set('checkpoint', 'safe dock');
  const store = create(db.indexedDB, () => { throw new Error('Legacy storage denied'); }, () => {});
  await store.initialize();
  assert.equal(store.load('save'), 'new progress');
  db.faults.read = true;
  const next = instance(db).store;
  await assert.rejects(next.initialize(), /Read unavailable/);
  assert.throws(() => next.store('save', 'blank'), /loading/);
  assert.equal(db.rows.get('save'), 'new progress');
});

test('shell gates startup and supplies persistent retry/export and unload protection', () => {
  assert.match(shell, /addRunDependency\("orebit-save-storage"\)/);
  assert.match(shell, /saveStorage.initialize\(\).then/);
  assert.match(shell, /Download recovery save/);
  assert.match(shell, /saveStorage.unsafeToLeave\(\)/);
  assert.match(shell, /saveStorage.flush\(\)/);
});

test('routine commits are silent; milestone notification waits for successful commit', async () => {
  const db = database(), { store, notices } = instance(db);
  await store.initialize();
  store.store('save', 'routine'); await store.flush();
  assert.equal(notices.some(n => n.milestone), false);
  db.faults.write = true;
  store.store('save', 'orbit', true); await store.flush();
  assert.equal(notices.some(n => n.milestone), false);
  db.faults.write = false; await store.flush();
  assert.equal(notices.filter(n => n.milestone).length, 1);
  store.store('save', 'routine later'); await store.flush();
  assert.equal(notices.filter(n => n.milestone).length, 1);
});

test('milestone cue is small, scene-anchored, spins once and suppresses frequent repeats', () => {
  assert.match(shell, /--ui-scene-y/);
  assert.match(shell, /--ui-scene-width, 100vw\) - 48px/);
  assert.match(shell, /duration: 1000, iterations: 1/);
  assert.match(shell, /now - lastSaveIndicatorTime < 20000/);
  assert.match(shell, /saveNotice.style.display = status.error \? "block" : "none"/);
  assert.doesNotMatch(shell, /Saving progress…/);
});
