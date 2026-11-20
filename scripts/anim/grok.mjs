// Grok Bot avatars as busy scenes, one per shape: art/grok/<shape>.svg (scripts/anim/grok_extract.mjs).
// gen_buddy_pets.mjs imports { sets, scenes }; importing writes nothing.
//
// Scene `grok_<shape>`: the body (white, tinted on the dial with the bot's colour: scene.tint names that layer)
// bobs, and the eyes ride it, glance left and right, and blink. Native size (the shapes are ~120 px across at
// 144, scaled by XL); the parts are vector, so a size change is XL.
import { readdirSync, readFileSync } from 'node:fs';
import { scaleScene } from './frames.mjs';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '../..');
const ART = join(ROOT, 'art/grok');
const N = 144;
const SCALE = 4 / 3;  // the old face size, where GAZE is measured

// 20 fps: the dial steps scenes from a 50 ms interval (buddy-ui.yaml), so a shorter step only skips steps unevenly.
const STEPS = 52, STEP_MS = 50;  // 2.6 s loop
const every = (f) => Array.from({ length: STEPS }, (_, s) => f(s));
const BOB = every((s) => -3 * (1 - Math.cos((2 * Math.PI * s) / 26)) / 2);  // two bobs per loop, 0..-3; scaleScene rounds
const LOOK = every((s) => (s >= 10 && s < 20 ? -3 : s >= 22 && s < 32 ? 3 : 0));        // glance left, then right
const BLINK = every((s) => (s >= 42 && s < 45 ? 1 : 0));                                  // eyes frame: 0 open, 1 shut
export const GAZE = { x: 20, y: 20 };  // IMU gaze reach in dial px (the scripted glance is 4)

// A frame cropped to its content box (1 px margin for anti-aliasing), placed where that box sits on the 144 canvas.
// Full-canvas frames cost 62 KB each decoded; the eyes alone are a tenth of that.
function frame(inner, [x0, y0, x1, y1]) {
  const x = Math.max(0, Math.floor(x0) - 1), y = Math.max(0, Math.floor(y0) - 1);
  const w = Math.min(N, Math.ceil(x1) + 1) - x, h = Math.min(N, Math.ceil(y1) + 1) - y;
  return { svg: `<svg xmlns="http://www.w3.org/2000/svg" width="${w}" height="${h}" viewBox="${x} ${y} ${w} ${h}">${inner}</svg>`, at: [x, y] };
}
// Box of an outline: its absolute coordinate pairs (the app writes M/L polylines); the sphere's arc is a disc.
function pathBox(d) {
  if (/a/i.test(d)) return [N / 2 - 61, N / 2 - 61, N / 2 + 61, N / 2 + 61];
  const v = d.match(/-?\d*\.?\d+/g).map(Number), xs = v.filter((_, i) => i % 2 === 0), ys = v.filter((_, i) => i % 2 === 1);
  return [Math.min(...xs), Math.min(...ys), Math.max(...xs), Math.max(...ys)];
}
function rectsBox(g) {
  const r = [...g.matchAll(/x="([\d.]+)" y="([\d.]+)" width="([\d.]+)" height="([\d.]+)"/g)].map((m) => m.slice(1).map(Number));
  return [Math.min(...r.map((q) => q[0])), Math.min(...r.map((q) => q[1])), Math.max(...r.map((q) => q[0] + q[2])), Math.max(...r.map((q) => q[1] + q[3]))];
}
// A blink squashes each capsule to a fifth of its height about its centre.
const shut = (eyes) => eyes.replace(/<rect x="([\d.]+)" y="([\d.]+)" width="([\d.]+)" height="([\d.]+)" rx="[\d.]+"\/>/g, (_, x, y, w, h) => {
  const hh = Math.max(+w * 0.6, +h / 5), yy = +y + (+h - hh) / 2;
  return `<rect x="${x}" y="${yy.toFixed(2)}" width="${w}" height="${hh.toFixed(2)}" rx="${(hh / 2).toFixed(2)}"/>`;
});

export const sets = {}, scenes = {};
// One scene per shape (`grok_<shape>`, XL): it fills the busy and idle face (the bots are all head).
// Bodies and eyes are A8 (alpha only: the body tinted on the dial, the eyes drawn in their layer's `ink`): a third of
// RGB565A8's PSRAM, which is what pays for XL.
const EYE_INK = 0x111111;  // art/grok eyes fill
export const XL = 2.3;  // face size, busy and idle: the bots are all head (also Copilot's head, scripts/anim/copilot.mjs)
const SIZES = [['', XL]];
for (const file of readdirSync(ART).filter((f) => f.endsWith('.svg')).sort()) {
  const shape = file.slice(0, -4), s = readFileSync(join(ART, file), 'utf8');
  const body = s.match(/<path id="body"[^>]*\/>/)[0], eyes = s.match(/<g id="eyes"[\s\S]*?<\/g>/)[0];
  const eyesShut = shut(eyes);
  for (const [size, k] of SIZES) {
    const name = `grok_${shape}${size}`, bs = `${name}_body`, es = `${name}_eyes`;
    sets[bs] = [{ ...frame(body, pathBox(body.match(/ d="([^"]*)"/)[1])), a8: true }];
    sets[es] = [{ ...frame(eyes, rectsBox(eyes)), a8: true }, { ...frame(eyesShut, rectsBox(eyesShut)), a8: true }];
    scenes[name] = {
      name, w: N, h: N, dx: 0, dy: 0, steps: STEPS, step_ms: STEP_MS, tint: 0,
      layers: [
        { set: bs, loop: every(() => 0), at: every((j) => [sets[bs][0].at[0], sets[bs][0].at[1] + BOB[j]]) },
        { set: es, ink: EYE_INK, loop: BLINK, at: every((j) => { const a = sets[es][BLINK[j]].at; return [a[0] + LOOK[j], a[1] + BOB[j]]; }) },
      ],
      discs: 0, disc: null, count_at: null,
    };
    scaleScene(k, { [bs]: sets[bs], [es]: sets[es] }, scenes[name].layers, scenes[name]);
    // IMU gaze (buddy/gaze.h): the eyes move up to GAZE px (at SCALE; XL scales it); look = the scaled glance,
    // taken out while the IMU steers.
    const eyesAt = scenes[name].layers[1].at, still = every((j) => sets[es][BLINK[j]].at), g = k / SCALE;
    scenes[name].gaze = { layer: 1, x: Math.round(GAZE.x * g), y: Math.round(GAZE.y * g),
                          look: eyesAt.map(([x], j) => x - Math.round(still[j][0] * k)) };
  }
}
