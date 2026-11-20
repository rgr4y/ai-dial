// GitHub Copilot's helmet head as two scenes: art/copilot/copilot.svg, redrawn flat after its logo.
// gen_buddy_pets.mjs imports { sets, scenes }; importing writes nothing.
//
// copilot_relax (its turn on the idle face): the head bobs, the eyes glance left and right and blink, goggles up.
// copilot_work (a busy Copilot CLI session: hook.sh copilot, heartbeat `agent`): goggles down over the visor, a quick
// bob, the eyes scan and pulse. Built like the Grok bots (scripts/anim/grok.mjs): parts cropped to their boxes on a
// 144 canvas, scaled by SCALE.
import { readFileSync } from 'node:fs';
import { scaleScene } from './frames.mjs';
import { GAZE, XL } from './grok.mjs';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '../..');
const SVG = readFileSync(join(ROOT, 'art/copilot/copilot.svg'), 'utf8');
const N = 144, SCALE = 4 / 3;  // same size as the Grok bots
const PLACE = 'translate(12 12) scale(.1)';  // the 1200 art onto the 144 canvas: 1150 wide -> 115

const part = (id) => SVG.match(new RegExp(`<path id="${id}"[^>]*/>|<g id="${id}"[\\s\\S]*?</g>`))[0];
const defs = SVG.match(/<defs>[\s\S]*?<\/defs>/)[0];
// A part cropped to its box (art units, 1 px margin), placed where that box sits on the 144 canvas.
function frame(inner, [x0, y0, x1, y1]) {
  const c = (v) => v * 0.1 + 12;
  const x = Math.floor(c(x0)) - 1, y = Math.floor(c(y0)) - 1, w = Math.ceil(c(x1)) + 1 - x, h = Math.ceil(c(y1)) + 1 - y;
  return { svg: `<svg xmlns="http://www.w3.org/2000/svg" width="${w}" height="${h}" viewBox="${x} ${y} ${w} ${h}">${defs}<g transform="${PLACE}">${inner}</g></svg>`, at: [x, y] };
}
const EYES_BOX = [432, 683, 766, 862];
const eyes = part('eyes');
// Blink: each pill squashed to a fifth of its height about its centre. Pulse: the same pills, dimmer.
const shut = eyes.replace(/y="683" width="94" height="179" rx="47"/g, 'y="755" width="94" height="36" rx="18"');
const dim = eyes.replace('#22F5FF', '#1497B8');

// Parts per size: '' (the busy and idle-turn size) and _xl (fills the idle face, like the Grok bots).
const parts = () => ({
  body: [frame(part('head') + part('visor'), [25, 132, 1175, 1065])],
  goggles: [frame(part('goggles'), [245, 258, 955, 545])],
  eyes: [frame(eyes, EYES_BOX), frame(shut, EYES_BOX), frame(dim, EYES_BOX)],
});
export const sets = {}, scenes = {};

const every = (n, f) => Array.from({ length: n }, (_, s) => f(s));
function scene(name, sz, steps, bob, goggleDy, look, eyeFrame) {
  const p = parts(), set = (k) => `copilot${sz}_${k}`;
  for (const k in p) sets[set(k)] ??= p[k];
  const at = (k, f, dx, dy) => [p[k][f].at[0] + dx, p[k][f].at[1] + dy];
  return {
    name, w: N, h: N, dx: 0, dy: 0, steps, step_ms: 50,
    layers: [
      { set: set('body'), loop: every(steps, () => 0), at: every(steps, (s) => at('body', 0, 0, bob[s])) },
      // Eyes last: in the work pose they glow over the lowered goggles.
      { set: set('goggles'), loop: every(steps, () => 0), at: every(steps, (s) => at('goggles', 0, 0, bob[s] + goggleDy)) },
      { set: set('eyes'), loop: every(steps, eyeFrame), at: every(steps, (s) => at('eyes', 0, look[s], bob[s])) },
    ],
    discs: 0, disc: null, count_at: null,
  };
}
// IMU gaze (buddy/gaze.h), as on the Grok bots: the eyes (layer 2) move up to GAZE px at full size (scaled with the
// scene); look = the scripted glance in dial px, taken out while the IMU steers.
function gaze(sc, k) {
  const g = k / SCALE, home = Math.round(sets[sc.layers[2].set][0].at[0] * k);  // frame `at` stays unscaled
  sc.gaze = { layer: 2, x: Math.round(GAZE.x * g), y: Math.round(GAZE.y * g), look: sc.layers[2].at.map(([x]) => x - home) };
}

// Relax: 2.6 s at 20 fps (the dial's 50 ms tick), the Grok bots' timing: two bobs, a glance each way, one blink.
const R = 52, rBob = every(R, (s) => -3 * (1 - Math.cos((2 * Math.PI * s) / 26)) / 2);
const rLook = every(R, (s) => (s >= 10 && s < 20 ? -3 : s >= 22 && s < 32 ? 3 : 0));
const rEyes = (s) => (s >= 42 && s < 45 ? 1 : 0);
// Work: 1.2 s, goggles down 30 px over the visor, a quicker shallow bob, the eyes scan left/right and pulse.
const W = 24, wBob = every(W, (s) => -2 * (1 - Math.cos((2 * Math.PI * s) / 12)) / 2);
const wLook = every(W, (s) => (s < 12 ? -2 : 2));

for (const [sz, k] of [['', SCALE], ['_xl', XL]]) {
  const relax = scene(`copilot_relax${sz}`, sz, R, rBob, 0, rLook, rEyes);
  const sc = { [`copilot_relax${sz}`]: relax };
  const own = Object.fromEntries(Object.keys(sets).filter((n) => n.startsWith(`copilot${sz}_`)).map((n) => [n, sets[n]]));
  if (sz === '') sc.copilot_work = scene('copilot_work', sz, W, wBob, 30, wLook, (s) => (s % 6 < 3 ? 0 : 2));
  scaleScene(k, own, Object.values(sc).flatMap((x) => x.layers), relax);
  if (sc.copilot_work) Object.assign(sc.copilot_work, { w: relax.w, h: relax.h, dx: relax.dx, dy: relax.dy });
  gaze(relax, k);
  if (sc.copilot_work) gaze(sc.copilot_work, k);
  Object.assign(scenes, sc);
}
