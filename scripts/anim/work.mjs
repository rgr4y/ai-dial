#!/usr/bin/env node
// Build the `work` (cooking) scene from the SVG parts in art/claude/ into a pack-shaped dir.
//
//     node scripts/anim/work.mjs [out-dir]        default build/anim/claude
//
// gen_buddy_pets.mjs imports { sets, scenes } from here to ship the scene in the pack; importing writes nothing.
// Same layout as send.mjs (read its header for the output shape and the diff command):
//
//     node scripts/anim/diff.mjs bridge/assets/claude work build/anim/claude work build/anim/work-diff.png
//
// Reused parts (body, legs, arms, eyes) are drawn in the send scene's coords; SH moves them into this scene's. The new
// parts (hat, pan, food-*) are drawn in this scene's coords. Rotation happens here at render time; the device only
// translates frames.
// Dependencies come from bridge/ (run `npm install` there once).
import { createHash } from 'node:crypto';
import { mkdirSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { frameFile, raw565a8 } from './frames.mjs';
import { render } from './render.mjs';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '../..');
const ART = join(ROOT, 'art/claude');
const OUT = resolve(process.argv[2] ?? join(ROOT, 'build/anim/claude'));

// ---- parts ---------------------------------------------------------------------------------------------------
const part = (name) => readFileSync(join(ART, `${name}.svg`), 'utf8');
const head = (s) => s.match(/<svg[^>]*>/)[0];
const attr = (s, a) => s.match(new RegExp(`\\b${a}="([^"]*)"`))[1];
const inner = (s) => s.slice(head(s).length + s.indexOf(head(s)), s.lastIndexOf('</svg>'));
const box = (s) => attr(head(s), 'viewBox').split(' ').map(Number);

// Send-scene art -> this scene: torso top-left 17,29 there, 13,38 here.
const SH = [-4, 9];
const GRIP = [114, 70];  // pan handle's left end: the pan turns about here

// A part as a frame at its own place: viewBox origin is its position (frame 0 pose), plus `shift`.
function plain(name, shift = [0, 0]) {
  const s = part(name), [x, y] = box(s);
  return { svg: s, at: [x + shift[0], y + shift[1]] };
}
// Several parts drawn back to front on one canvas (the union of their boxes).
function stack(names, shift = [0, 0]) {
  const ss = names.map(part), bs = ss.map(box);
  const x0 = Math.min(...bs.map((b) => b[0])), y0 = Math.min(...bs.map((b) => b[1]));
  const x1 = Math.max(...bs.map((b) => b[0] + b[2])), y1 = Math.max(...bs.map((b) => b[1] + b[3]));
  return { svg: `<svg xmlns="http://www.w3.org/2000/svg" width="${x1 - x0}" height="${y1 - y0}" viewBox="${x0} ${y0} ${x1 - x0} ${y1 - y0}">${ss.map(inner).join('')}</svg>`, at: [x0 + shift[0], y0 + shift[1]] };
}
// Part turned by `deg` (positive = clockwise) about `pivot` on the smallest integer canvas that holds it; `at` is
// where that canvas sits in the scene.
function turned(name, deg, pivot) {
  const s = part(name), [bx, by, bw, bh] = box(s), a = (deg * Math.PI) / 180, xs = [], ys = [];
  for (const [x, y] of [[bx, by], [bx + bw, by], [bx, by + bh], [bx + bw, by + bh]]) {
    const dx = x - pivot[0], dy = y - pivot[1];
    xs.push(pivot[0] + dx * Math.cos(a) - dy * Math.sin(a));
    ys.push(pivot[1] + dx * Math.sin(a) + dy * Math.cos(a));
  }
  const x0 = Math.floor(Math.min(...xs)), y0 = Math.floor(Math.min(...ys));
  const w = Math.ceil(Math.max(...xs)) - x0, h = Math.ceil(Math.max(...ys)) - y0;
  return { svg: `<svg xmlns="http://www.w3.org/2000/svg" width="${w}" height="${h}" viewBox="${x0} ${y0} ${w} ${h}"><g transform="rotate(${deg} ${pivot[0]} ${pivot[1]})">${inner(s)}</g></svg>`, at: [x0, y0] };
}
// Food turned about its centre on a fixed square canvas (HALF either side), so every frame has the same size and
// the centre sits at `at` + HALF.
const spun = (name, deg, [cx, cy], half) => ({
  svg: `<svg xmlns="http://www.w3.org/2000/svg" width="${2 * half}" height="${2 * half}" viewBox="${cx - half} ${cy - half} ${2 * half} ${2 * half}"><g transform="rotate(${deg} ${cx} ${cy})">${inner(part(name))}</g></svg>`,
  at: [cx - half, cy - half],
});

// ---- motion tables (one entry per step) ----------------------------------------------------------------------
const STEPS = 26, STEP_MS = 55;
const every = (f) => Array.from({ length: STEPS }, (_, s) => f(s));
const from = (pairs, d = 0) => every((s) => pairs[s] ?? d);  // sparse { step: value } -> per step
// Pan: wind-up dip (tip down, grip down), flick up at 10-13, settle back. From the upstream frames' handle/pan centroids.
const ANG = from({ 7: 3, 8: 6, 9: 3, 10: -6, 11: -14, 12: -14, 13: -14, 14: -10, 15: -7, 16: -4, 17: -2, 18: -1 });
const GDY = from({ 7: 2, 8: 4, 9: 1, 10: -4, 11: -8, 12: -8, 13: -7, 14: -6, 15: -5, 16: -2, 17: -1 });
// Torso bob (1 px up) as upstream; the hat hops on the toss.
const BOB = every((s) => (s >= 1 && s <= 5) || (s >= 14 && s <= 18) ? -1 : 0);
const HOP = from({ 10: -1, 11: -2, 12: -2, 13: -1 });
const HAPPY = every((s) => (s >= 9 && s <= 12 ? 1 : 0));  // ^ ^ eyes while the food goes up
const GY = every((s) => GDY[s] + BOB[s]);  // the arm and pan ride the grip

// Food: rest centre in the pan, canvas half-size, launch step L (first step in the air), land step E (back at rest),
// peak height H above the rest spot, sideways bump at the peak, whole turns in the air. Staggered on purpose.
const FOODS = [
  { name: 'orange', c: [150, 58], half: 10, L: 10, E: 20, H: 62, bump: -5, turns: 2 },
  { name: 'broccoli', c: [164, 53], half: 14, L: 11, E: 22, H: 66, bump: 2, turns: 1 },
  { name: 'tomato', c: [180, 58], half: 10, L: 10, E: 21, H: 78, bump: 8, turns: 2 },
];
const SPIN = 12;  // frames per turn

// Where something resting at `c` in the pan is on step s: turned about the grip, then lifted with it.
const rad = (d) => (d * Math.PI) / 180;
const carried = ([x, y], s) => {
  const dx = x - GRIP[0], dy = y - GRIP[1], a = rad(ANG[s]);
  return [GRIP[0] + dx * Math.cos(a) - dy * Math.sin(a), GRIP[1] + dx * Math.sin(a) + dy * Math.cos(a) + GY[s]];
};
// Centre and spin frame of a food on step s: on the pan outside [L, E), on its own parabola inside.
function flight(f, s) {
  if (s < f.L || s >= f.E) return { p: carried(f.c, s), k: 0 };
  const a = carried(f.c, f.L - 1), b = carried(f.c, f.E), u = (s - (f.L - 1)) / (f.E - (f.L - 1));
  return {
    p: [a[0] + (b[0] - a[0]) * u + f.bump * Math.sin(Math.PI * u), a[1] + (b[1] - a[1]) * u - 4 * f.H * u * (1 - u)],
    k: Math.round(f.turns * SPIN * u) % SPIN,
  };
}

// ---- frame sets ----------------------------------------------------------------------------------------------
// Each set is a list of { svg, at }; `at` is the frame's scene position. Layers address a frame by set + index.
const PAN_ANGS = [...new Set(ANG)].sort((a, b) => a - b);
export const sets = {
  work_base: [stack(['arm-down', 'legs', 'body'], SH)],
  work_arm: [plain('arm-right', SH)],
  work_eyes: [plain('eyes-open', SH), plain('eyes-happy', SH)],
  work_hat: [plain('hat')],
  ...Object.fromEntries(FOODS.map((f) => [`work_${f.name}`, Array.from({ length: SPIN }, (_, k) => spun(`food-${f.name}`, (360 * k) / SPIN, f.c, f.half))])),
  work_pan: PAN_ANGS.map((a) => turned('pan', a, GRIP)),
};

// ---- scene ---------------------------------------------------------------------------------------------------
const place = (set, k, dx, dy) => [sets[set][k].at[0] + dx, sets[set][k].at[1] + dy];
const layers = [
  { set: 'work_base', loop: every(() => 0), at: every((s) => place('work_base', 0, 0, BOB[s])) },
  { set: 'work_arm', loop: every(() => 0), at: every((s) => place('work_arm', 0, 0, GY[s])) },
  { set: 'work_eyes', loop: HAPPY, at: every((s) => place('work_eyes', HAPPY[s], 0, BOB[s])) },
  { set: 'work_hat', loop: every(() => 0), at: every((s) => place('work_hat', 0, 0, BOB[s] + HOP[s])) },
  ...FOODS.map((f) => {
    const fl = every((s) => flight(f, s));
    return { set: `work_${f.name}`, loop: fl.map(({ k }) => k), at: fl.map(({ p }) => [Math.round(p[0]) - f.half, Math.round(p[1]) - f.half]) };
  }),
  // last = in front of the food, so it sinks behind the rim as it lands
  { set: 'work_pan', loop: every((s) => PAN_ANGS.indexOf(ANG[s])), at: every((s) => place('work_pan', PAN_ANGS.indexOf(ANG[s]), 0, GY[s])) },
];
export const scenes = {
  work: { name: 'claude_work', w: 117, h: 117, dx: 1, dy: -4, steps: STEPS, step_ms: STEP_MS, layers, discs: 0, disc: null, count_at: null },
};

// ---- write ---------------------------------------------------------------------------------------------------
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
  console.log(`work: ${Object.keys(files).length} frames, ${bytes} bytes packed, rev ${rev} -> ${OUT}`);
}
