import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync, readdirSync } from 'node:fs';
import { createHash } from 'node:crypto';
import { fileURLToPath } from 'node:url';
import path from 'node:path';

const root = fileURLToPath(new URL('../', import.meta.url));
const audio = path.join(root, 'assets/audio');
const manifest = JSON.parse(readFileSync(path.join(audio, 'manifest.json')));
test('every catalog cue has a unique licensed PCM WAV with matching provenance hash', () => {
  const catalog = readFileSync(path.join(root, 'src/platform/GameAudioCatalog.h'), 'utf8');
  const paths = [...catalog.matchAll(/\{"([^"]+\.wav)"/g)].map(m => m[1]);
  assert.equal(new Set(paths).size, paths.length);
  assert.deepEqual(manifest.map(m => m.file).sort(), paths.sort());
  const shipped = ['ui', 'surface', 'gameplay'].flatMap(folder =>
    readdirSync(path.join(audio, folder)).filter(f => f.endsWith('.wav')).map(f => `${folder}/${f}`));
  assert.deepEqual(shipped.sort(), paths.sort());
  for (const row of manifest) {
    assert.equal(row.license, 'CC0-1.0');
    assert.match(row.original_sha256, /^[a-f0-9]{64}$/);
    assert.match(row.source, /^https:\/\/(kenney.nl|opengameart.org)\//);
    const data = readFileSync(path.join(audio, row.file));
    assert.equal(createHash('sha256').update(data).digest('hex'), row.sha256);
    assert.equal(data.toString('ascii', 0, 4), 'RIFF');
    assert.equal(data.toString('ascii', 8, 12), 'WAVE');
    let pcm, rate;
    for (let offset = 12; offset + 8 <= data.length;) {
      const length = data.readUInt32LE(offset + 4);
      const kind = data.toString('ascii', offset, offset + 4);
      if (kind === 'fmt ') {
        assert.equal(data.readUInt16LE(offset + 8), 1);
        assert.equal(data.readUInt16LE(offset + 10), 1);
        rate = data.readUInt32LE(offset + 12);
        assert.equal(data.readUInt16LE(offset + 22), 16);
      }
      if (kind === 'data') pcm = data.subarray(offset + 8, offset + 8 + length);
      offset += 8 + length + (length % 2);
    }
    assert.ok(pcm?.length > 0 && rate > 0);
    assert.ok(Math.abs(pcm.length / (2 * rate) - row.duration) < .001);
    let peak = 0;
    for (let i = 0; i < pcm.length; i += 2) peak = Math.max(peak, Math.abs(pcm.readInt16LE(i)) / 32768);
    assert.ok(peak > .001 && peak <= row.peak + .001, row.file);
  }
});
test('native and web resolve the same catalog and bound simultaneous playback', () => {
  for (const file of ['src/platform/sdl/SdlGameAudio.cpp', 'src/platform/web/WebMain.cpp']) {
    const source = readFileSync(path.join(root, file), 'utf8');
    assert.ok(source.includes('audioCueCatalog['));
    assert.ok(!source.includes('switch (cue)'));
  }
  const web = readFileSync(path.join(root, 'src/platform/web/WebMain.cpp'), 'utf8');
  assert.ok(web.includes('audio.failures.has(path)'));
  assert.ok(web.includes('if (pending) return 1'));
  assert.ok(web.includes('audio.voices >= 8'));
});
