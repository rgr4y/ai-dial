#!/usr/bin/env node
// Build the `send` scene from the SVG parts in art/claude/ into a pack-shaped dir.
//
//     node scripts/anim/send.mjs [out-dir]        default build/anim/claude
//
// gen_buddy_pets.mjs imports { sets, scenes, raw565a8 } from here to ship the scene in the pack; importing writes nothing.
//
// Output: <out>/<set>/<k>.bin (one LVGL RGB565A8 RLE frame each) and <out>/manifest.json (the scene table, same shape
// as bridge/assets/claude/manifest.json). Check it against today's scene with
//
//     node scripts/anim/diff.mjs bridge/assets/claude send build/anim/claude send build/anim/send-diff.png
//
// Rotation, scale and clipping happen here at render time; the device only translates frames.
// rle/unrle/frameFile are copied from gen_buddy_pets.mjs: that file has no main guard, so importing it would run the
// generator and rewrite bridge/assets/claude. Keep them in step with it.
// Dependencies come from bridge/ (run `npm install` there once).
import { createHash } from 'node:crypto';
import { mkdirSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { render } from './render.mjs';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '../..');
const ART = join(ROOT, 'art/claude');
const OUT = resolve(process.argv[2] ?? join(ROOT, 'build/anim/claude'));

// ---- LVGL frame files (copy of gen_buddy_pets.mjs) ------------------------------------------------------------
const BLK = 2;
function rle(raw) {
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
function unrle(src, outLen) {
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

// ---- parts ---------------------------------------------------------------------------------------------------
const part = (name) => readFileSync(join(ART, `${name}.svg`), 'utf8');
const head = (s) => s.match(/<svg[^>]*>/)[0];
const attr = (s, a) => s.match(new RegExp(`\\b${a}="([^"]*)"`))[1];
const inner = (s) => s.slice(head(s).length + s.indexOf(head(s)), s.lastIndexOf('</svg>'));

// A part as a frame at its own place: viewBox origin is its position in scene coords (frame 0 pose).
function plain(name) {
  const s = part(name), [x, y, w, h] = attr(head(s), 'viewBox').split(' ').map(Number);
  return { svg: s, at: [x, y], w, h };
}
// Same, with `g` wrapped around the content (clip, flip, scale about a point...) on the part's own canvas.
function wrapped(name, wrap) {
  const s = part(name), h = head(s);
  return { svg: `${h}${wrap(inner(s))}</svg>`, at: attr(h, 'viewBox').split(' ').slice(0, 2).map(Number) };
}

// Letter art is 28x19 with its own origin at the letter's top-left.
const LETTER_W = 28, LETTER_H = 19;
const letterClip = (w) => ({ svg: part('letter').replace(/width="28"/, `width="${w}"`).replace('viewBox="0 0 28 19"', `viewBox="0 0 ${w} 19"`), w, h: LETTER_H });
// Flag art lies flat, 28x10, pivot on the ball. Frame = flag turned by `deg` (negative = up) about the pivot, on the
// smallest integer canvas that holds it; `at` is where that canvas sits in the scene.
const FLAG_AT = [187, 37], PIVOT = [2.5, 3];
function flagTurned(deg) {
  const a = (deg * Math.PI) / 180, xs = [], ys = [];
  for (const [x, y] of [[0, 0], [28, 0], [0, 10], [28, 10]]) {
    const dx = x - PIVOT[0], dy = y - PIVOT[1];
    xs.push(PIVOT[0] + dx * Math.cos(a) - dy * Math.sin(a));
    ys.push(PIVOT[1] + dx * Math.sin(a) + dy * Math.cos(a));
  }
  const x0 = Math.floor(Math.min(...xs)), y0 = Math.floor(Math.min(...ys));
  const w = Math.ceil(Math.max(...xs)) - x0, h = Math.ceil(Math.max(...ys)) - y0;
  return {
    svg: `<svg xmlns="http://www.w3.org/2000/svg" width="${w}" height="${h}" viewBox="${x0} ${y0} ${w} ${h}"><g transform="rotate(${deg} ${PIVOT[0]} ${PIVOT[1]})">${inner(part('flag'))}</g></svg>`,
    at: [FLAG_AT[0] + x0, FLAG_AT[1] + y0],
  };
}

// ---- frame sets ----------------------------------------------------------------------------------------------
// Each set is a list of { svg, at }; `at` is the frame's scene position. Layers address a frame by set + index.
const BLANK = { svg: '<svg xmlns="http://www.w3.org/2000/svg" width="1" height="1"/>', at: [0, 0] };  // hides a layer
export const sets = {
  send_mailbox: [plain('mailbox')],
  send_arm_l: [plain('arm-down'), plain('arm-up')],
  send_legs: [plain('legs')],
  send_body: [plain('body')],
  // second arm frame is the first flipped top to bottom (the waving poses light it from the other side)
  send_arm_r: [plain('arm-right'), wrapped('arm-right', (c) => `<g transform="translate(0 125) scale(1 -1)">${c}</g>`)],
  send_eyes: [plain('eyes-open'), plain('eyes-happy')],
  // 0 blank, 1 whole, 2-4 sliding into the mailbox slot (clipped at its edge), 5-8 growing back out
  send_letter: [BLANK, letterClip(28), letterClip(18), letterClip(11), letterClip(4)],
  // 0 flat, then raised and lowered again (-90 is straight up)
  send_flag: [flagTurned(0), ...[-15, -61, -79, -83, -86, -88, -62, -21].map(flagTurned)],
};

// ---- scene ---------------------------------------------------------------------------------------------------
const STEPS = 40, STEP_MS = 60;
const pose = [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 2, 2, 2, 1, 3, 1, 2, 2, 2, 1, 3, 0, 0, 0, 0, 0, 0, 0, 0];
const POSE_DY = [0, -3, -6, 0];  // torso height per pose: crouch, rise, peak, settle
const at0 = (set, k, dx = 0, dy = 0) => [sets[set][k].at[0] + dx, sets[set][k].at[1] + dy];
const every = (f) => Array.from({ length: STEPS }, (_, s) => f(s));
const lift = (set, k) => every((s) => at0(set, k, 0, POSE_DY[pose[s]]));
const byPose = (...frames) => every((s) => frames[pose[s]]);

// Letter and flag by step: letter [frame, x, y] (blank = hidden), flag frame (index into send_flag).
const L = (k, x, y) => [k, x, y];
const letter = [
  ...Array(10).fill(L(1, 111, 41)), L(1, 114, 40), L(1, 119, 37), L(1, 126, 35), L(2, 133, 32), L(3, 140, 29), L(4, 147, 26),
  ...Array(24).fill(L(0, 111, 41)),
].slice(0, STEPS);
const flag = [...Array(17).fill(0), 1, 2, 3, 4, 5, ...Array(12).fill(6), 7, 8, 0, 0, 0, 0];

const layers = [
  { set: 'send_mailbox', loop: every(() => 0), at: every(() => at0('send_mailbox', 0)) },
  { set: 'send_arm_l', loop: byPose(0, 1, 1, 1), at: every((s) => at0('send_arm_l', pose[s] ? 1 : 0, 0, POSE_DY[pose[s]])) },
  { set: 'send_legs', loop: every(() => 0), at: lift('send_legs', 0) },
  { set: 'send_body', loop: every(() => 0), at: lift('send_body', 0) },
  { set: 'send_arm_r', loop: byPose(0, 1, 1, 0), at: every((s) => at0('send_arm_r', pose[s] === 1 || pose[s] === 2 ? 1 : 0, 0, POSE_DY[pose[s]])) },
  { set: 'send_eyes', loop: byPose(0, 1, 1, 1), at: every((s) => at0('send_eyes', pose[s] ? 1 : 0, 0, POSE_DY[pose[s]])) },
  { set: 'send_letter', loop: letter.map(([k]) => k), at: letter.map(([, x, y]) => [x, y]) },
  { set: 'send_flag', loop: flag, at: flag.map((k) => sets.send_flag[k].at) },
];
export const scenes = {
  send: { name: 'claude_send', w: 192, h: 109, dx: 0, dy: 14, steps: STEPS, step_ms: STEP_MS, layers, discs: 0, disc: null, count_at: null },
};

// ---- write ---------------------------------------------------------------------------------------------------
export function raw565a8({ w, h, rgba }) {
  const n = w * h, raw = Buffer.alloc(n * 3);
  for (let i = 0; i < n; i++) {
    const r = rgba[i * 4], g = rgba[i * 4 + 1], b = rgba[i * 4 + 2];
    raw.writeUInt16LE(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3), i * 2);  // as gen_buddy_pets.mjs; art colours are the shipped pack's decoded values
    raw[n * 2 + i] = rgba[i * 4 + 3];
  }
  return raw;
}

if (process.argv[1] === fileURLToPath(import.meta.url)) {
  rmSync(OUT, { recursive: true, force: true });
  const files = {}, manifestSets = {};
  for (const [name, frames] of Object.entries(sets)) {
    mkdirSync(join(OUT, name), { recursive: true });
    manifestSets[name] = frames.map((f, k) => {
      const px = render(f.svg), file = frameFile(px.w, px.h, raw565a8(px)), path = `${name}/${k}.bin`;
      f.w = px.w; f.h = px.h;
      writeFileSync(join(OUT, path), file);
      files[path] = { size: file.length, sha256: createHash('sha256').update(file).digest('hex') };
      return path;
    });
  }
  const rev = createHash('sha256').update(JSON.stringify([files, scenes])).digest('hex').slice(0, 16);
  writeFileSync(join(OUT, 'manifest.json'), JSON.stringify({ name: 'claude', rev, files, sets: manifestSets, scenes }));
  const bytes = Object.values(files).reduce((a, f) => a + f.size, 0);
  console.log(`send: ${Object.keys(files).length} frames, ${bytes} bytes packed, rev ${rev} -> ${OUT}`);
}
