// GitHub Copilot's helmet head as two scenes, from art/copilot/idle.svg: the modern mascot's idle loop, a 32 x 1728
// strip of 54 one-colour 32 x 32 cells. A few of its poses (front, turned right, turned left) are recoloured in
// art/copilot/copilot.svg's palette: helmet, goggle lenses and visor gradients, cyan eyes.
// gen_buddy_pets.mjs imports { sets, scenes }; importing writes nothing.
//
// copilot_relax (its turn on the idle face): the head bobs, turns right then left, and blinks.
// copilot_work (a busy Copilot CLI session: hook.sh copilot, heartbeat `agent`): a quick bob, the head scans
// right and left, the eyes pulse. The strip has no goggles-down pose, so work is the idle head, busier.
//
// Two layers: the head (helmet, lenses, visor; one frame per pose) and the eyes (one frame per pose and look: open,
// shut, dim), so the IMU gaze can move the eyes. Each frame is cropped to its own box and placed per step.
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { createRequire } from 'node:module';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { scaleScene } from './frames.mjs';
import { GAZE, XL } from './grok.mjs';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '../..');
const { Resvg } = createRequire(join(ROOT, 'bridge/package.json'))('@resvg/resvg-js');
const STRIP = readFileSync(join(ROOT, 'art/copilot/idle.svg'), 'utf8');
const N = 144, SCALE = 4 / 3, CELL = 32;  // same size as the Grok bots
const HEAD_W = 115;  // front head width on the 144 canvas, as the old flat head

// Strip cells used: [head pose, its open eyes, its shut eyes (a blink cell of the same pose)].
const POSES = [[0, 0, 2], [16, 16, 21], [33, 33, 36]];  // front, turned right, turned left
const FRONT = 0, RIGHT = 1, LEFT = 2;
const OPEN = 0, SHUT = 1, DIM = 2;

// Paths binned by cell (file order isn't cell order). Each cell: one 4-subpath head (outline, then holes for the
// visor and both lenses) and two eye paths.
const cells = [];
for (const [, d] of STRIP.matchAll(/<path[^>]*\bd="([^"]*)"/g)) {
  const y = Number(d.match(/^M\s*-?[\d.]+[,\s]+(-?[\d.]+)/)[1]);
  (cells[Math.floor(y / CELL)] ??= []).push(d);
}
const bbox = (d) => {
  const b = new Resvg(`<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 ${CELL} ${CELL * cells.length}"><path d="${d}"/></svg>`).getBBox();
  return [b.x, b.y, b.x + b.width, b.y + b.height];
};
function cell(i) {
  const ps = [...cells[i]].sort((a, b) => b.length - a.length);
  assert.equal(ps.length, 3, `cell ${i}: head + 2 eyes`);
  const sub = ps[0].split(/(?=M)/).map((s) => s.trim());
  assert.equal(sub.length, 4, `cell ${i}: head outline + 3 holes`);
  // Holes by area: the visor is the biggest, the lenses the other two.
  const area = (d) => { const [x0, y0, x1, y1] = bbox(d); return (x1 - x0) * (y1 - y0); };
  const [visor, ...lenses] = sub.slice(1).sort((a, b) => area(b) - area(a));
  return { head: ps[0], outline: sub[0], visor, lenses, eyes: ps.slice(1), y: i * CELL };
}

const defs = `<defs>
<linearGradient id="g-head" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#B16CFF"/><stop offset="1" stop-color="#2E62FF"/></linearGradient>
<linearGradient id="g-lens" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#7B10F5"/><stop offset="1" stop-color="#1735C0"/></linearGradient>
<linearGradient id="g-visor" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#0A2296"/><stop offset="1" stop-color="#060F70"/></linearGradient>
</defs>`;
// Lenses and visor first, then the head with its holes on top: the holes show the colours through, and the head's
// anti-aliased edges sit over them instead of over transparency.
const headSvg = (c) =>
  `<path fill="url(#g-visor)" d="${c.visor}"/>${c.lenses.map((d) => `<path fill="url(#g-lens)" d="${d}"/>`).join('')}` +
  `<path fill="url(#g-head)" fill-rule="evenodd" d="${c.head}"/>`;
const eyesSvg = (c, fill) => `<g fill="${fill}">${c.eyes.map((d) => `<path d="${d}"/>`).join('')}</g>`;

// Cell units -> the 144 canvas: the front head HEAD_W wide, centred. Every cell shares that mapping, so the poses
// keep their own sway inside the cell.
const fc = cell(POSES[FRONT][0]), front = bbox(fc.outline);
const S = HEAD_W / (front[2] - front[0]);
const OX = N / 2 - S * (front[0] + front[2]) / 2, OY = N / 2 - S * ((front[1] + front[3]) / 2 - fc.y);
const place = (c) => `translate(${OX} ${OY}) scale(${S}) translate(0 ${-c.y})`;
// One part cropped to its box in cell units (1 px margin), placed where that box sits on the 144 canvas.
function frame(c, inner, [x0, y0, x1, y1]) {
  const cx = (v) => v * S + OX, cy = (v) => (v - c.y) * S + OY;
  const x = Math.floor(cx(x0)) - 1, y = Math.floor(cy(y0)) - 1, w = Math.ceil(cx(x1)) + 1 - x, h = Math.ceil(cy(y1)) + 1 - y;
  return { svg: `<svg xmlns="http://www.w3.org/2000/svg" width="${w}" height="${h}" viewBox="${x} ${y} ${w} ${h}">${defs}<g transform="${place(c)}">${inner}</g></svg>`, at: [x, y] };
}
// Box around paths, in strip units.
const pathsBox = (ds) => { const bs = ds.map(bbox); return [0, 1, 2, 3].map((i) => (i < 2 ? Math.min : Math.max)(...bs.map((b) => b[i]))); };

const parts = () => {
  const poses = POSES.map(([h, o, s]) => ({ head: cell(h), open: cell(o), shut: cell(s) }));
  const eyeFrame = (c, fill) => frame(c, eyesSvg(c, fill), pathsBox(c.eyes));
  return {
    body: poses.map((p) => frame(p.head, headSvg(p.head), pathsBox([p.head.outline]))),
    eyes: poses.flatMap((p) => [eyeFrame(p.open, '#22F5FF'), eyeFrame(p.shut, '#22F5FF'), eyeFrame(p.open, '#1497B8')]),
  };
};
export const sets = {}, scenes = {};

const every = (n, f) => Array.from({ length: n }, (_, s) => f(s));
function scene(name, sz, steps, bob, pose, look) {
  const p = parts(), set = (k) => `copilot${sz}_${k}`;
  for (const k in p) sets[set(k)] ??= p[k];
  const at = (k, f, dy) => [p[k][f].at[0], p[k][f].at[1] + dy];
  return {
    name, w: N, h: N, dx: 0, dy: 0, steps, step_ms: 50,
    layers: [
      { set: set('body'), loop: every(steps, pose), at: every(steps, (s) => at('body', pose(s), bob[s])) },
      { set: set('eyes'), loop: every(steps, (s) => pose(s) * 3 + look(s)), at: every(steps, (s) => at('eyes', pose(s) * 3 + look(s), bob[s])) },
    ],
    discs: 0, disc: null, count_at: null,
  };
}
// IMU gaze (buddy/gaze.h), as on the Grok bots: the eyes (layer 1) move up to GAZE px at full size (scaled with the
// scene). No scripted glance in the eyes layer's x (the head turns are in the frames), so look is all zero.
function gaze(sc, k) {
  const g = k / SCALE;
  sc.gaze = { layer: 1, x: Math.round(GAZE.x * g), y: Math.round(GAZE.y * g), look: every(sc.steps, () => 0) };
}

// Relax: 2.6 s at 20 fps (the dial's 50 ms tick), the Grok bots' timing: two bobs, a turn each way, one blink.
const R = 52, rBob = every(R, (s) => -3 * (1 - Math.cos((2 * Math.PI * s) / 26)) / 2);
const rPose = (s) => (s >= 10 && s < 20 ? RIGHT : s >= 22 && s < 32 ? LEFT : FRONT);
const rLook = (s) => (s >= 42 && s < 45 ? SHUT : OPEN);
// Work: 1.2 s, a quicker shallow bob, the head scans right and left, the eyes pulse.
const W = 24, wBob = every(W, (s) => -2 * (1 - Math.cos((2 * Math.PI * s) / 12)) / 2);
const wPose = (s) => (s < 4 ? FRONT : s < 11 ? RIGHT : s < 14 ? FRONT : s < 21 ? LEFT : FRONT);
const wLook = (s) => (s % 6 < 3 ? OPEN : DIM);

for (const [sz, k] of [['', SCALE], ['_xl', XL]]) {
  const relax = scene(`copilot_relax${sz}`, sz, R, rBob, rPose, rLook);
  const sc = { [`copilot_relax${sz}`]: relax };
  const own = Object.fromEntries(Object.keys(sets).filter((n) => n.startsWith(`copilot${sz}_`)).map((n) => [n, sets[n]]));
  if (sz === '') sc.copilot_work = scene('copilot_work', sz, W, wBob, wPose, wLook);
  scaleScene(k, own, Object.values(sc).flatMap((x) => x.layers), relax);
  if (sc.copilot_work) Object.assign(sc.copilot_work, { w: relax.w, h: relax.h, dx: relax.dx, dy: relax.dy });
  for (const s of Object.values(sc)) gaze(s, k);
  Object.assign(scenes, sc);
}
