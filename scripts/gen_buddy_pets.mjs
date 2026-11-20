#!/usr/bin/env node
// Turn the decoded Harness Claude pet (docs/harness-pets, made by harness_pets_export.mjs) into a character pack the
// dial syncs into LittleFS from the bridge.
//
//     node scripts/gen_buddy_pets.mjs [docs/harness-pets] [bridge/assets/claude]
//
// Dependencies come from bridge/ (run `npm install` there once).
//
// Output:
//   <out>/<frame set>/<k>.bin  one frame: an LVGL 9 image file (lv_image_header_t, RGB565A8, flag COMPRESSED), then
//                              lv_image_compressed_t's 12-byte header and the RGB565A8 bytes (RGB565 plane, A8 plane)
//                              in LVGL's RLE on 2-byte blocks. The dial expands each frame once into PSRAM at load.
//   <out>/manifest.json        rev, every file's size + sha256, frame sets, scene tables (per-step body frame, step dy,
//                              overlay frame + offset, juggling discs, alert count position) and the small pet's loop.
//                              buddy/scene.h plays them with the same maths as focus.c (origin, step, overlay).
import { createHash } from 'node:crypto';
import { existsSync, mkdirSync, readFileSync, renameSync, rmSync, writeFileSync } from 'node:fs';
import { createRequire } from 'node:module';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { CF_A8, frameFile, raw565a8, rawA8 } from './anim/frames.mjs';
import { scenes as sendScenes, sets as sendSets } from './anim/send.mjs';
import { scenes as denyScenes, sets as denySets } from './anim/deny_send.mjs';
import { scenes as workScenes, sets as workSets } from './anim/work.mjs';
import { scenes as grokScenes, sets as grokSets } from './anim/grok.mjs';
import { scenes as copilotScenes, sets as copilotSets } from './anim/copilot.mjs';
import { render } from './anim/render.mjs';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const { PNG } = createRequire(join(ROOT, 'bridge/package.json'))('pngjs');
const SRC = process.argv[2] ?? join(ROOT, 'docs/harness-pets');
const OUT = process.argv[3] ?? join(ROOT, 'bridge/assets/claude');

const js = readFileSync(join(SRC, 'data.js'), 'utf8');
const data = JSON.parse(js.slice(js.indexOf('{'), js.lastIndexOf('}') + 1));
const pet = data.pets.find((p) => p.engine === 'claude');
const scriptedScenes = { ...sendScenes, ...denyScenes, ...workScenes, ...grokScenes, ...copilotScenes };
const scriptedSets = { ...sendSets, ...denySets, ...workSets, ...grokSets, ...copilotSets };


const files = {}, sets = {}, blobs = {};  // blobs: path -> bytes, written only if the rev changed
let rawBytes = 0, packedBytes = 0;

// Pack one frame (straight-alpha RGBA) as <name>/<k>.bin.
// a8: alpha only (frames.mjs rawA8), for art the dial tints.
function addFrame(name, k, w, h, rgba, a8 = false) {
  const raw = a8 ? rawA8({ w, h, rgba }) : raw565a8({ w, h, rgba });
  const file = a8 ? frameFile(w, h, raw, CF_A8) : frameFile(w, h, raw), path = `${name}/${k}.bin`;
  blobs[path] = file;
  files[path] = { size: file.length, sha256: createHash('sha256').update(file).digest('hex') };
  sets[name].push(path);
  rawBytes += raw.length;
  packedBytes += file.length;
}

// Write one frame set once; return its name.
function frames(fr, name) {
  name ??= fr.dir.split('/').at(-1);
  if (sets[name]) return name;
  sets[name] = [];
  for (let k = 0; k < fr.n; k++) {
    const { width: w, height: h, data: px } = PNG.sync.read(readFileSync(join(SRC, fr.dir, `${String(k).padStart(2, '0')}.png`)));
    addFrame(name, k, w, h, px);
  }
  return name;
}

// Scenes by role; the dial asks for these names (buddy/ui.h). listening = voice: not used.
// Codex's pet rides along as codex_relax (its turn on the idle face) and codex_work (a busy Codex session: hook.sh
// codex, heartbeat `agent`).
const scenes = {};
const codex = data.pets.find((p) => p.engine === 'codex');
for (const [src, prefix, roles] of [
  [pet, '', [['work', 'working'], ['build', 'working_alt'], ['send', 'sending'], ['alert', 'alert'], ['relax', 'relaxing']]],
  [codex, 'codex_', [['work', 'working'], ['relax', 'relaxing']]],
]) for (const [role, key] of roles) {
  const sc = src[key];
  if (!sc || (!prefix && scriptedScenes[role])) continue;  // scripted scenes (scripts/anim/) replace the upstream one
  const steps = sc.steps;  // level 0 only (listening has 5 levels for voice loudness; we use the quiet one)
  scenes[prefix + role] = {
    name: sc.name, w: sc.w, h: sc.h, dx: sc.dx, dy: sc.dy, steps, step_ms: sc.step_ms,
    // Layers back to front: body (translate by step_dy; missing = 0), then overlay.
    layers: [
      ...(sc.frames ? [{ set: frames(sc.frames), loop: sc.loop ? sc.loop.slice(0, steps) : null,
        at: Array.from({ length: steps }, (_, k) => [0, sc.step_dy ? sc.step_dy[k] : 0]) }] : []),
      ...(sc.overlay ? [{ set: frames(sc.overlay.frames), loop: sc.overlay.loop.slice(0, steps), at: sc.overlay.at.slice(0, steps) }] : []),
    ],
    // Ring arc with half 180 and width >= 2r is a filled disc of radius r + w/2: [cx16, cy16, r16, 0xRRGGBB] per
    // step x discs.
    discs: sc.shapes ? sc.shapes.count : 0,
    disc: sc.shapes ? sc.shapes.at.map(([cx, cy, r, w, , , c]) => [cx, cy, r + Math.floor(w / 2), (c[0] << 16) | (c[1] << 8) | c[2]]) : null,
    count_at: sc.count_at ? sc.count_at.slice(0, steps) : null,
  };
}

// Scripted scenes: parts rendered from art/claude/ by scripts/anim/ (each scene is its own module with the same exports; merge it in above).
for (const [name, fr] of Object.entries(scriptedSets)) {
  sets[name] = [];
  fr.forEach((f, k) => { const px = render(f.svg); addFrame(name, k, px.w, px.h, px.rgba, f.a8); });
}
Object.assign(scenes, scriptedScenes);

// Small pet above the recap / question card: 1.5x (zoom 6/8 of the 2x art) at <= 3 text lines, 1x (4/8) at 4
// (focus.c:886-889). 4 state loops of (frame, dy) at step_ms; all four are identical for Claude (pets.c:18).
const small = {
  z6: frames({ dir: pet.zoom['6'], n: pet.small.n }, 'claude_small_z6'),
  z4: frames({ dir: pet.zoom['4'], n: pet.small.n }, 'claude_small_z4'),
  loop: pet.loops[0], step_ms: pet.step_ms[0], steps: pet.steps,
};

const rev = createHash('sha256').update(JSON.stringify([files, scenes, small])).digest('hex').slice(0, 16);
const manifest = JSON.stringify({ name: 'claude', rev, files, sets, scenes, small });
console.log(`frames: ${rawBytes >> 10} KB raw, ${packedBytes >> 10} KB packed in ${Object.keys(files).length} files`);
let have = null;
try { have = JSON.parse(readFileSync(join(OUT, 'manifest.json'), 'utf8')).rev; } catch {}
if (have === rev) {
  console.log(`${OUT} unchanged, rev ${rev}`);
} else {
  // Build next to OUT, then swap, so the bridge never serves a half-written pack.
  const tmp = `${OUT}.tmp`, old = `${OUT}.old`;
  rmSync(tmp, { recursive: true, force: true });
  for (const [path, file] of Object.entries(blobs)) {
    mkdirSync(dirname(join(tmp, path)), { recursive: true });
    writeFileSync(join(tmp, path), file);
  }
  writeFileSync(join(tmp, 'manifest.json'), manifest);
  rmSync(old, { recursive: true, force: true });
  if (existsSync(OUT)) renameSync(OUT, old);
  renameSync(tmp, OUT);
  rmSync(old, { recursive: true, force: true });
  console.log(`wrote ${OUT}/manifest.json rev ${rev} (was ${have ?? 'none'})`);
}
