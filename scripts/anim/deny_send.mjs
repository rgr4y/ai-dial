#!/usr/bin/env node
// Build the `deny_send` scene: Clawd, angry, hurls the letter into the mailbox, which shakes. Reuses the send parts.
//
//     node scripts/anim/deny_send.mjs [out-dir]     default build/anim/claude (send and deny_send, one pack)
//
// gen_buddy_pets.mjs imports { sets, scenes } from here; importing writes nothing. Plays once (no letter comes back).
import { createHash } from 'node:crypto';
import { mkdirSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { frameFile, raw565a8 } from './frames.mjs';
import { render } from './render.mjs';
import { scenes as sendScenes, sets as sendSets } from './send.mjs';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '../..');
const OUT = resolve(process.argv[2] ?? join(ROOT, 'build/anim/claude'));

const part = (name) => readFileSync(join(ROOT, 'art/claude', `${name}.svg`), 'utf8');
const plain = (name) => {
  const s = part(name), [x, y, w, h] = s.match(/<svg[^>]*viewBox="([^"]*)"/)[1].split(' ').map(Number);
  return { svg: s, at: [x, y], w, h };
};
const BLANK = { svg: '<svg xmlns="http://www.w3.org/2000/svg" width="1" height="1"/>', at: [0, 0] };

// New frames only; mailbox, arms, legs, body, letter and flag come from send's sets.
export const sets = {
  deny_eyes: [plain('eyes-open'), plain('eyes-angry')],
  deny_anger: [BLANK, plain('anger')],
};

const STEPS = 40, STEP_MS = 60;
const S = sendSets;
const at0 = (set, k, dx = 0, dy = 0) => { const [x, y] = (S[set] ?? sets[set])[k].at; return [x + dx, y + dy]; };
const every = (f) => Array.from({ length: STEPS }, (_, s) => f(s));
// pose: 0 idle, 1 wind-up (crouch), 2 throw (leap), 3 follow-through
const pose = every((s) => (s < 2 ? 0 : s < 8 ? 1 : s < 12 ? 2 : s < 14 ? 3 : 0));
const POSE_DY = [0, 2, -4, 0];
const lift = (set, k) => every((s) => at0(set, k, 0, POSE_DY[pose[s]]));
const angry = every((s) => (s >= 2 && s < 32 ? 1 : 0));
const markOn = (s) => s >= 4 && s < 30;
const shake = every((s) => [2, -2, 2, -2, 1, -1, 1, -1, 0][s - 12] ?? 0);  // mailbox and flag, from the hit at step 12

// Letter by step: [frame, x, y]; a harder throw than send (bigger strides), gone once it is in the slot.
const throwPath = [[1, 122, 36], [2, 133, 32], [3, 140, 29], [4, 147, 26]];
const letter = every((s) => (s < 8 ? [1, 111, 41] : throwPath[s - 8] ?? [0, 111, 41]));

const layers = [
  { set: 'send_mailbox', loop: every(() => 0), at: shake.map((dx) => at0('send_mailbox', 0, dx)) },
  { set: 'send_arm_l', loop: every(() => 0), at: lift('send_arm_l', 0) },
  { set: 'send_legs', loop: every(() => 0), at: lift('send_legs', 0) },
  { set: 'send_body', loop: every(() => 0), at: lift('send_body', 0) },
  { set: 'send_arm_r', loop: every((s) => (pose[s] === 2 ? 1 : 0)), at: every((s) => at0('send_arm_r', pose[s] === 2 ? 1 : 0, 0, POSE_DY[pose[s]])) },
  { set: 'deny_eyes', loop: angry, at: every((s) => at0('deny_eyes', angry[s], 0, POSE_DY[pose[s]])) },
  { set: 'send_letter', loop: letter.map(([k]) => k), at: letter.map(([, x, y]) => [x, y]) },
  { set: 'send_flag', loop: every(() => 0), at: shake.map((dx) => at0('send_flag', 0, dx)) },
  // anger mark: pops in before the throw, bobs 1 px every 2 steps, rides the torso
  { set: 'deny_anger', loop: every((s) => (markOn(s) ? 1 : 0)), at: every((s) => at0('deny_anger', markOn(s) ? 1 : 0, 0, POSE_DY[pose[s]] - (s >> 1 & 1))) },
];
export const scenes = {
  deny_send: { name: 'claude_deny_send', w: 192, h: 109, dx: 0, dy: 14, steps: STEPS, step_ms: STEP_MS, layers, discs: 0, disc: null, count_at: null },
};

if (process.argv[1] === fileURLToPath(import.meta.url)) {
  rmSync(OUT, { recursive: true, force: true });
  const files = {}, manifestSets = {};
  for (const [name, frames] of Object.entries({ ...sendSets, ...sets })) {
    mkdirSync(join(OUT, name), { recursive: true });
    manifestSets[name] = frames.map((f, k) => {
      const px = render(f.svg), file = frameFile(px.w, px.h, raw565a8(px)), path = `${name}/${k}.bin`;
      writeFileSync(join(OUT, path), file);
      files[path] = { size: file.length, sha256: createHash('sha256').update(file).digest('hex') };
      return path;
    });
  }
  const all = { ...sendScenes, ...scenes };
  const rev = createHash('sha256').update(JSON.stringify([files, all])).digest('hex').slice(0, 16);
  writeFileSync(join(OUT, 'manifest.json'), JSON.stringify({ name: 'claude', rev, files, sets: manifestSets, scenes: all }));
  console.log(`deny_send: ${Object.keys(files).length} frames, rev ${rev} -> ${OUT}`);
}
