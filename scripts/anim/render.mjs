#!/usr/bin/env node
// Deterministic SVG -> straight-alpha RGBA, for pet part art.
//
//     node scripts/anim/render.mjs in.svg out.png
//     node scripts/anim/render.mjs --self-test
//
// Dependencies come from bridge/ (run `npm install` there once).
import assert from 'node:assert/strict';
import { readFileSync, writeFileSync } from 'node:fs';
import { createRequire } from 'node:module';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '../..');
const req = createRequire(join(ROOT, 'bridge/package.json'));
const { Resvg } = req('@resvg/resvg-js');
const { PNG } = req('pngjs');

export function render(svg, { scale = 1 } = {}) {
  const img = new Resvg(svg, {
    fitTo: { mode: 'zoom', value: scale },
    font: { loadSystemFonts: false },
  }).render();
  // resvg-js .pixels is premultiplied (verified: 50% red -> 128,0,0,128); convert to straight alpha.
  const rgba = Buffer.from(img.pixels);
  for (let i = 0; i < rgba.length; i += 4) {
    const a = rgba[i + 3];
    if (a > 0 && a < 255) for (let c = 0; c < 3; c++) rgba[i + c] = Math.min(255, Math.round(rgba[i + c] * 255 / a));
  }
  return { w: img.width, h: img.height, rgba };
}

export function writePng(path, { w, h, rgba }) {
  const png = new PNG({ width: w, height: h });
  rgba.copy(png.data);
  writeFileSync(path, PNG.sync.write(png));
}

function selfTest() {
  const { w, h, rgba } = render(
    '<svg xmlns="http://www.w3.org/2000/svg" width="4" height="4"><rect x="1" y="1" width="2" height="2" fill="#f00"/></svg>');
  assert.equal(w, 4);
  assert.equal(h, 4);
  assert.equal(rgba.length, 64);
  for (let y = 0; y < 4; y++) for (let x = 0; x < 4; x++) {
    const p = [...rgba.subarray((y * 4 + x) * 4, (y * 4 + x) * 4 + 4)];
    const inside = x >= 1 && x <= 2 && y >= 1 && y <= 2;
    assert.deepEqual(p, inside ? [255, 0, 0, 255] : [0, 0, 0, 0], `pixel ${x},${y}`);
  }
  const half = render('<svg xmlns="http://www.w3.org/2000/svg" width="1" height="1"><rect width="1" height="1" fill="#f00" fill-opacity="0.5"/></svg>');
  assert.deepEqual([...half.rgba], [255, 0, 0, 128], 'straight alpha');
  console.log('render self-test ok');
}

if (process.argv[1] === fileURLToPath(import.meta.url)) {
  const [a, b] = process.argv.slice(2);
  if (a === '--self-test') selfTest();
  else if (a && b) writePng(b, render(readFileSync(a, 'utf8')));
  else { console.error('usage: render.mjs in.svg out.png | --self-test'); process.exit(2); }
}
