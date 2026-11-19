#!/usr/bin/env node
// One-off: pull the Grok Bot avatar shapes out of the installed app into art/grok/<shape>.svg.
//
//     node scripts/anim/grok_extract.mjs ["/Applications/Grok Bot.app"]
//
// The app draws each bot as a code-generated outline (one of ~26 shapes) with two capsule eyes placed from a
// per-shape table. This loads the app's own renderer chunk from app.asar (read-only), calls its outline generator
// and eye table, and writes one SVG per shape: a white `body` path (tinted per bot on the dial) and dark `eyes`.
// The art is checked in, so the pack build never needs the app. Re-run after an app update only to pick up new
// shapes; the minified names it looks for change between app versions, and it says which one it couldn't find.
import { mkdirSync, openSync, readSync, rmSync, writeFileSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '../..');
const APP = process.argv[2] ?? '/Applications/Grok Bot.app';
const ASAR = join(APP, 'Contents/Resources/app.asar');
const OUT = join(ROOT, 'art/grok');
const WORK = join(ROOT, 'build/grok-src');
const N = 144;  // canvas: the app's own avatar size unit; the pack scales it

// ---- asar: header is [4][header size][4][json length][json]; file offsets count from 8 + header size ----
const fd = openSync(ASAR, 'r');
const head16 = Buffer.alloc(16);
readSync(fd, head16, 0, 16, 0);
const json = Buffer.alloc(head16.readUInt32LE(12));
readSync(fd, json, 0, json.length, 16);
const tree = JSON.parse(json.toString()), base = 8 + head16.readUInt32LE(4);
const files = new Map();
(function walk(node, p) {
  for (const [k, v] of Object.entries(node.files || {})) {
    if (v.files) walk(v, `${p}${k}/`);
    else if (!v.unpacked) files.set(`${p}${k}`, v);
  }
})(tree, '');
const read = (path) => {
  const v = files.get(path), b = Buffer.alloc(v.size);
  readSync(fd, b, 0, v.size, base + Number(v.offset));
  return b.toString('utf8');
};

// ---- find the renderer chunk and the names inside it ----
const ASSETS = 'dist/renderer/assets/';
const chunk = [...files.keys()].filter((p) => p.startsWith(ASSETS) && p.endsWith('.js'))
  .find((p) => read(p).includes('Motion Lab defines exactly two eye capsules'));
if (!chunk) throw new Error('renderer chunk not found (marker "Motion Lab defines exactly two eye capsules")');
let src = read(chunk);
const need = (what, re) => {
  const m = src.match(re);
  if (!m) throw new Error(`${what} not found in ${chunk}: the app changed; update the pattern`);
  return m[1];
};
const SHAPES = need('shape list', /(\w+)=`sphere\.[a-zA-Z0-9.]+`\.split\(`\.`\)/);
const RENDER = need('outline renderer', /function (\w+)\(e,t,n,r\)\{let i=r==null\?\[\]:\w+\(e,t,n,r\);return e===`pumpkin`/);
const EYES = need('eye table', /(\w+)=\{cloud:\{width:/);

// Its only import is the bundler runtime; copy both next to each other and export what we need.
rmSync(WORK, { recursive: true, force: true });
mkdirSync(WORK, { recursive: true });
for (const [, dep] of src.matchAll(/from"\.\/([^"]+)"/g)) writeFileSync(join(WORK, dep), read(ASSETS + dep));
src += `\nexport { ${SHAPES} as __shapes, ${RENDER} as __render, ${EYES} as __eyes };\n`;
writeFileSync(join(WORK, 'renderer.mjs'), src);
const m = await import(pathToFileURL(join(WORK, 'renderer.mjs')).href);

// ---- write ----
rmSync(OUT, { recursive: true, force: true });
mkdirSync(OUT, { recursive: true });
const H = N / 2, f = (v) => +v.toFixed(2);
const written = [];
for (const shape of m.__shapes) {
  let body;
  try {
    body = m.__render(shape, { yaw: 0, pitch: 0, roll: 0 }, N, null).body.join('');
  } catch {
    if (shape !== 'sphere') throw new Error(`no outline for ${shape}`);
    body = `M${H} ${H - 60}a60 60 0 1 1 -.01 0Z`;  // the sphere has its own shaded renderer: a plain disc here
  }
  // Eye table units are half the canvas: width/height of one capsule, gap between centres, shiftY down from centre.
  const e = m.__eyes[shape] ?? m.__eyes.cloud;
  const w = e.width * H, h = e.height * H, g = e.gap * H, cy = H + e.shiftY * H;
  const eye = (cx) => `<rect x="${f(cx - w / 2)}" y="${f(cy - h / 2)}" width="${f(w)}" height="${f(h)}" rx="${f(w / 2)}"/>`;
  writeFileSync(join(OUT, `${shape}.svg`), `<svg xmlns="http://www.w3.org/2000/svg" width="${N}" height="${N}" viewBox="0 0 ${N} ${N}">
  <path id="body" d="${body}" fill="#fff" fill-rule="evenodd"/>
  <g id="eyes" fill="#111">${eye(H - g / 2)}${eye(H + g / 2)}</g>
</svg>
`);
  written.push(shape);
}
console.log(`${written.length} shapes from ${chunk} -> ${OUT}: ${written.join(' ')}`);
