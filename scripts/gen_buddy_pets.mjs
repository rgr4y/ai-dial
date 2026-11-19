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

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const { PNG } = createRequire(join(ROOT, 'bridge/package.json'))('pngjs');
const SRC = process.argv[2] ?? join(ROOT, 'docs/harness-pets');
const OUT = process.argv[3] ?? join(ROOT, 'bridge/assets/claude');

const js = readFileSync(join(SRC, 'data.js'), 'utf8');
const data = JSON.parse(js.slice(js.indexOf('{'), js.lastIndexOf('}') + 1));
const pet = data.pets.find((p) => p.engine === 'claude');

// LVGL RLE (libs/rle/lv_rle.c) on 2-byte blocks, as lv_bin_decoder uses for RGB565A8. A control byte with bit 7 set
// is followed by (ctrl & 0x7f) literal blocks; otherwise one block repeats ctrl times. The blob is padded to whole
// blocks and the last block always goes out as a literal, so a decoder that truncates at the output size never
// drops real bytes.
const BLK = 2;
export function rle(raw) {
  const buf = Buffer.concat([raw, Buffer.alloc(raw.length % BLK)]);
  const n = buf.length / BLK, last = n - 1, out = [];
  const same = (a, b) => buf.compare(buf, b * BLK, b * BLK + BLK, a * BLK, a * BLK + BLK) === 0;
  let i = 0;
  while (i < last) {
    let r = 1;
    while (i + r < last && r < 127 && same(i, i + r)) r++;
    if (r >= 2) {
      out.push(Buffer.from([r]), buf.subarray(i * BLK, i * BLK + BLK));
      i += r;
      continue;
    }
    let l = 1;
    while (i + l < last && l < 127 && !(i + l + 1 < last && same(i + l, i + l + 1))) l++;
    out.push(Buffer.from([0x80 | l]), buf.subarray(i * BLK, (i + l) * BLK));
    i += l;
  }
  out.push(Buffer.from([0x81]), buf.subarray(last * BLK));
  return Buffer.concat(out);
}
// Mirrors LVGL lv_rle_decompress, for the round-trip check.
export function unrle(src, outLen) {
  const out = Buffer.alloc(outLen + BLK);
  let r = 0, w = 0;
  while (r < src.length && w < outLen) {
    const c = src[r++];
    if (c & 0x80) {
      const b = (c & 0x7f) * BLK;
      src.copy(out, w, r, r + b);
      r += b; w += b;
    } else {
      for (let k = 0; k < c; k++) src.copy(out, w + k * BLK, r, r + BLK);
      r += BLK; w += c * BLK;
    }
  }
  return out.subarray(0, outLen);
}

// lv_image_header_t (magic 0x19, cf RGB565A8 0x14, flags COMPRESSED 0x08, w, h, stride) + lv_image_compressed_t
// header (method RLE = 1, compressed size, decompressed size) + RLE body.
function frameFile(w, h, raw) {
  const body = rle(raw);
  if (!unrle(body, raw.length).equals(raw)) throw new Error('RLE round trip failed');
  const head = Buffer.alloc(24);
  head.writeUInt8(0x19, 0);
  head.writeUInt8(0x14, 1);
  head.writeUInt16LE(0x08, 2);
  head.writeUInt16LE(w, 4);
  head.writeUInt16LE(h, 6);
  head.writeUInt16LE(w * 2, 8);
  head.writeUInt32LE(1, 12);
  head.writeUInt32LE(body.length, 16);
  head.writeUInt32LE(raw.length, 20);
  return Buffer.concat([head, body]);
}

const files = {}, sets = {}, blobs = {};  // blobs: path -> bytes, written only if the rev changed
let rawBytes = 0, packedBytes = 0;

// Write one frame set once; return its name.
function frames(fr, name) {
  name ??= fr.dir.split('/').at(-1);
  if (sets[name]) return name;
  sets[name] = [];
  for (let k = 0; k < fr.n; k++) {
    const { width: w, height: h, data: px } = PNG.sync.read(readFileSync(join(SRC, fr.dir, `${String(k).padStart(2, '0')}.png`)));
    const n = w * h, raw = Buffer.alloc(n * 3);
    for (let i = 0; i < n; i++) {
      const r = px[i * 4], g = px[i * 4 + 1], b = px[i * 4 + 2];
      raw.writeUInt16LE(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3), i * 2);  // little-endian RGB565, as LVGL stores it
      raw[n * 2 + i] = px[i * 4 + 3];
    }
    const file = frameFile(w, h, raw), path = `${name}/${k}.bin`;
    blobs[path] = file;
    files[path] = { size: file.length, sha256: createHash('sha256').update(file).digest('hex') };
    sets[name].push(path);
    rawBytes += raw.length;
    packedBytes += file.length;
  }
  return name;
}

// Scenes by role; the dial asks for these names (buddy/ui.h). listening = voice: not used.
const scenes = {};
for (const [role, key] of [['work', 'working'], ['build', 'working_alt'], ['send', 'sending'], ['alert', 'alert'], ['relax', 'relaxing']]) {
  const sc = pet[key];
  if (!sc) continue;
  const steps = sc.steps;  // level 0 only (listening has 5 levels for voice loudness; we use the quiet one)
  scenes[role] = {
    name: sc.name, w: sc.w, h: sc.h, dx: sc.dx, dy: sc.dy, steps, step_ms: sc.step_ms,
    body: sc.frames ? frames(sc.frames) : null,
    loop: sc.loop ? sc.loop.slice(0, steps) : null,
    step_dy: sc.step_dy ? sc.step_dy.slice(0, steps) : null,
    ov: sc.overlay ? frames(sc.overlay.frames) : null,
    ov_loop: sc.overlay ? sc.overlay.loop.slice(0, steps) : null,
    ov_at: sc.overlay ? sc.overlay.at.slice(0, steps) : null,
    // Ring arc with half 180 and width >= 2r is a filled disc of radius r + w/2: [cx16, cy16, r16, 0xRRGGBB] per
    // step x discs.
    discs: sc.shapes ? sc.shapes.count : 0,
    disc: sc.shapes ? sc.shapes.at.map(([cx, cy, r, w, , , c]) => [cx, cy, r + Math.floor(w / 2), (c[0] << 16) | (c[1] << 8) | c[2]]) : null,
    count_at: sc.count_at ? sc.count_at.slice(0, steps) : null,
  };
}

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
