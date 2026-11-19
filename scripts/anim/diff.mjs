#!/usr/bin/env node
// Composite pack scenes step by step and diff two of them.
//
//     node scripts/anim/diff.mjs <packA> <sceneA> <packB> <sceneB> [out.png]
//     node scripts/anim/diff.mjs --self-test
//
// A pack is a character pack dir (manifest.json + <set>/<k>.bin). Pass a dir of PNG sets (<dir>/<set>/<NN>.png, no
// manifest) as a pack by giving a manifest-less dir plus a scene JSON path as the scene name.
// Dependencies come from bridge/ (run `npm install` there once).
import assert from 'node:assert/strict';
import { existsSync, readFileSync, writeFileSync } from 'node:fs';
import { createRequire } from 'node:module';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { unrle } from './frames.mjs';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '../..');
const { PNG } = createRequire(join(ROOT, 'bridge/package.json'))('pngjs');
const TOL = 8;

// .bin frame (24-byte lv_image_header_t + compressed header, RLE body of RGB565 plane then A8 plane) -> RGBA.
function decodeBin(buf) {
  const w = buf.readUInt16LE(4), h = buf.readUInt16LE(6), n = w * h;
  assert.equal(buf[0], 0x19, 'not an LVGL image');
  assert.equal(buf.readUInt32LE(20), n * 3, 'size mismatch');
  const raw = unrle(buf.subarray(24), n * 3);
  const rgba = Buffer.alloc(n * 4);
  for (let i = 0; i < n; i++) {
    const p = raw.readUInt16LE(i * 2), r = p >> 11, g = (p >> 5) & 63, b = p & 31;
    rgba[i * 4] = (r << 3) | (r >> 2);
    rgba[i * 4 + 1] = (g << 2) | (g >> 4);
    rgba[i * 4 + 2] = (b << 3) | (b >> 2);
    rgba[i * 4 + 3] = raw[n * 2 + i];
  }
  return { w, h, rgba };
}

// Pack dir -> { scene, loadFrame }. Scene is a manifest scene name.
export function packLoader(dir, name) {
  const m = JSON.parse(readFileSync(join(dir, 'manifest.json'), 'utf8'));
  const scene = m.scenes[name];
  if (!scene) throw new Error(`no scene "${name}" in ${dir}: ${Object.keys(m.scenes).join(', ')}`);
  return { scene, loadFrame: (set, k) => decodeBin(readFileSync(join(dir, m.sets[set][k]))) };
}

// Dir of PNG sets (<dir>/<set>/<NN>.png) + a scene JSON file.
export function pngLoader(dir, sceneFile) {
  const scene = JSON.parse(readFileSync(sceneFile, 'utf8'));
  return {
    scene,
    loadFrame(set, k) {
      const p = PNG.sync.read(readFileSync(join(dir, set, String(k).padStart(2, '0') + '.png')));
      return { w: p.width, h: p.height, rgba: p.data };
    },
  };
}

// One straight-alpha RGBA { w, h, rgba } per step, layers composited in order over transparent, in scene-local space
// (origin 0,0). Canvas is scene w x h, grown to fit layers that land outside; pass `size` to force a common canvas.
export function composite(scene, loadFrame, size) {
  const cache = new Map();
  const get = (set, k) => {
    const key = `${set}/${k}`;
    if (!cache.has(key)) cache.set(key, loadFrame(set, k));
    return cache.get(key);
  };
  const place = (l, s) => {
    const f = get(l.set, l.loop[s]);
    return { f, x: l.at[s][0], y: l.at[s][1] };
  };
  let x0 = 0, y0 = 0, x1 = scene.w, y1 = scene.h;
  if (!size) {
    for (let s = 0; s < scene.steps; s++) for (const l of scene.layers) {
      const { f, x, y } = place(l, s);
      x0 = Math.min(x0, x); y0 = Math.min(y0, y); x1 = Math.max(x1, x + f.w); y1 = Math.max(y1, y + f.h);
    }
  } else ({ x0, y0, x1, y1 } = size);
  const W = x1 - x0, H = y1 - y0, steps = [];
  for (let s = 0; s < scene.steps; s++) {
    const out = Buffer.alloc(W * H * 4);
    for (const l of scene.layers) {
      const { f, x, y } = place(l, s);
      for (let j = 0; j < f.h; j++) for (let i = 0; i < f.w; i++) {
        const px = x - x0 + i, py = y - y0 + j;
        if (px < 0 || py < 0 || px >= W || py >= H) continue;
        const si = (j * f.w + i) * 4, di = (py * W + px) * 4;
        const sa = f.rgba[si + 3] / 255, da = out[di + 3] / 255, oa = sa + da * (1 - sa);
        if (oa === 0) continue;
        for (let c = 0; c < 3; c++) out[di + c] = Math.round((f.rgba[si + c] * sa + out[di + c] * da * (1 - sa)) / oa);
        out[di + 3] = Math.round(oa * 255);
      }
    }
    steps.push({ w: W, h: H, rgba: out });
  }
  return { steps, box: { x0, y0, x1, y1 } };
}

const bounds = (c) => c.box;

// Per-step diff of two loaders' scenes on a common canvas. Transparent pixels compare equal whatever their RGB.
export function diff(A, B) {
  const a0 = composite(A.scene, A.loadFrame), b0 = composite(B.scene, B.loadFrame);
  const size = {
    x0: Math.min(bounds(a0).x0, bounds(b0).x0), y0: Math.min(bounds(a0).y0, bounds(b0).y0),
    x1: Math.max(bounds(a0).x1, bounds(b0).x1), y1: Math.max(bounds(a0).y1, bounds(b0).y1),
  };
  const a = composite(A.scene, A.loadFrame, size).steps, b = composite(B.scene, B.loadFrame, size).steps;
  const W = size.x1 - size.x0, H = size.y1 - size.y0, n = Math.max(a.length, b.length);
  const blank = { w: W, h: H, rgba: Buffer.alloc(W * H * 4) };
  const rows = [];
  for (let s = 0; s < n; s++) {
    const fa = a[s] ?? blank, fb = b[s] ?? blank, mask = Buffer.alloc(W * H * 4);
    let count = 0, max = 0;
    for (let i = 0; i < W * H; i++) {
      const o = i * 4, aa = fa.rgba[o + 3], ba = fb.rgba[o + 3];
      let d = Math.abs(aa - ba);
      if (aa && ba) for (let c = 0; c < 3; c++) d = Math.max(d, Math.abs(fa.rgba[o + c] - fb.rgba[o + c]));
      max = Math.max(max, d);
      if (d > TOL) { count++; mask[o] = 255; mask[o + 3] = 255; }
    }
    rows.push({ a: fa, b: fb, mask, count, max });
  }
  return { rows, W, H };
}

// Strip: one row per step, A | B | mask, on a grey checker-free mid background.
function strip({ rows, W, H }) {
  const png = new PNG({ width: W * 3 + 8, height: rows.length * (H + 2) });
  png.data.fill(0);
  for (let i = 0; i < png.data.length; i += 4) { png.data[i] = png.data[i + 1] = png.data[i + 2] = 96; png.data[i + 3] = 255; }
  rows.forEach((r, s) => [r.a.rgba, r.b.rgba, r.mask].forEach((src, col) => {
    for (let j = 0; j < H; j++) for (let i = 0; i < W; i++) {
      const si = (j * W + i) * 4, di = ((s * (H + 2) + j) * png.width + col * (W + 4) + i) * 4, al = src[si + 3] / 255;
      for (let c = 0; c < 3; c++) png.data[di + c] = Math.round(src[si + c] * al + png.data[di + c] * (1 - al));
    }
  }));
  return PNG.sync.write(png);
}

function selfTest() {
  const send = () => packLoader(join(ROOT, 'bridge/assets/claude'), 'send');
  const { rows } = diff(send(), send());
  assert.ok(rows.length > 0);
  assert.ok(rows.every((r) => r.count === 0 && r.max === 0), 'send vs send differs');
  // synthetic: 2x1 opaque red layer under a half-alpha blue pixel, offset by (1,0), grows canvas to 3 wide
  const px = (r, g, b, a) => ({ w: 1, h: 1, rgba: Buffer.from([r, g, b, a]) });
  const f = { r: px(255, 0, 0, 255), b: px(0, 0, 255, 128) };
  const sc = { w: 1, h: 1, steps: 1, layers: [{ set: 'r', loop: [0], at: [[0, 0]] }, { set: 'b', loop: [0], at: [[1, 0]] }] };
  const c = composite(sc, (set) => f[set]);
  assert.equal(c.steps[0].w, 2);
  assert.deepEqual([...c.steps[0].rgba.subarray(0, 4)], [255, 0, 0, 255]);
  assert.deepEqual([...c.steps[0].rgba.subarray(4, 8)], [0, 0, 255, 128]);
  console.log('diff self-test ok');
}

const args = process.argv.slice(2);
if (args[0] === '--self-test') selfTest();
else if (args.length >= 4) {
  const load = (d, s) => (existsSync(join(d, 'manifest.json')) ? packLoader(d, s) : pngLoader(d, s));
  const res = diff(load(args[0], args[1]), load(args[2], args[3]));
  let tot = 0, max = 0;
  res.rows.forEach((r, s) => {
    console.log(`step ${String(s).padStart(3)}  diff px ${String(r.count).padStart(6)}  max delta ${r.max}`);
    tot += r.count; max = Math.max(max, r.max);
  });
  console.log(`total ${tot} differing px over ${res.rows.length} steps, max delta ${max}`);
  if (args[4]) { writeFileSync(args[4], strip(res)); console.log(`wrote ${args[4]}`); }
} else {
  console.error('usage: diff.mjs <packA> <sceneA> <packB> <sceneB> [out.png] | --self-test');
  process.exit(2);
}
