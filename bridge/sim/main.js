// Dial simulator: draws what the dial reports it is showing (the `ui` JSON, docs/DIAL-PROTOCOL.md) with the same
// art and maths the firmware uses (buddy/scene.h, buddy/ui.h), and sends clicks back as taps.
// Pets come from window.HT (docs/harness-pets/data.js, decoded from Harness pets.c).

const W = 466, C = 233;
const canvas = document.getElementById('glass');
const ctx = canvas.getContext('2d');
const $ = (id) => document.getElementById(id);

// ---- art ----
const pet = window.HT.pets.find((p) => p.engine === 'claude');
const SCENES = {};
for (const k of ['working', 'working_alt', 'sending', 'alert', 'relaxing']) if (pet[k]) SCENES[pet[k].name] = pet[k];
const imgs = new Map();
function img(src) {
  let i = imgs.get(src);
  if (!i) { i = new Image(); i.src = src; imgs.set(src, i); }
  return i;
}
const pad = (k) => String(k).padStart(2, '0');
function sprite(dir, k, x, y) {
  const i = img(`/${dir}/${pad(k)}.png`);
  if (i.complete && i.naturalWidth) ctx.drawImage(i, x, y);
  return i;
}
// Preload so the first loop already has frames.
for (const sc of Object.values(SCENES)) {
  for (const fr of [sc.frames, sc.overlay?.frames]) if (fr) for (let k = 0; k < fr.n; k++) img(`/${fr.dir}/${pad(k)}.png`);
}

// ---- scene maths (buddy/scene.h) ----
const step = (sc, t) => Math.floor(t / sc.step_ms) % sc.steps;
const origin = (sc, bias) => [((W - sc.w) >> 1) + sc.dx, C - (sc.h >> 1) + bias + sc.dy];

function drawScene(sc, t, bias) {
  const [x, y] = origin(sc, bias), k = step(sc, t);
  if (sc.frames) sprite(sc.frames.dir, sc.loop[k], x, y + (sc.step_dy ? sc.step_dy[k] : 0));
  if (sc.overlay) sprite(sc.overlay.frames.dir, sc.overlay.loop[k], x + sc.overlay.at[k][0], y + sc.overlay.at[k][1]);
  if (sc.shapes) {
    // The firmware draws each juggling ball as a filled disc of radius (r + w/2) / 16 (gen_buddy_pets.mjs).
    for (let i = 0; i < sc.shapes.count; i++) {
      const [cx, cy, r16, w16, , , rgb] = sc.shapes.at[k * sc.shapes.count + i];
      const r = (r16 + (w16 >> 1)) >> 4;
      ctx.fillStyle = `rgb(${rgb})`;
      ctx.beginPath();
      ctx.arc(x + (cx >> 4), y + (cy >> 4), r, 0, Math.PI * 2);
      ctx.fill();
    }
  }
}

// Alert bubble: plays once, holds its last step, placed from the working scene's origin (focus.c:259-269, :961).
function drawAlert(base, t, count) {
  const a = SCENES.claude_alert;
  const k = Math.min(Math.floor(t / a.step_ms), a.steps - 1);
  const [x, y] = origin(base, 4), o = a.overlay;
  sprite(o.frames.dir, o.loop[k], x + o.at[k][0], y + o.at[k][1]);
  const ca = a.count_at[k];
  if (ca[0] || ca[1]) {
    ctx.font = count.length > 1 ? '400 20px Inter' : '500 26px Inter';
    ctx.fillStyle = '#006fff';
    ctx.textAlign = 'center';
    ctx.textBaseline = 'middle';
    ctx.fillText(count, x + ca[0], y + ca[1]);
  }
  return [x + o.at[k][0], y + o.at[k][1]];
}

// ---- text ----
function wrap(text, font, width, maxLines) {
  ctx.font = font;
  const words = text.split(/\s+/), lines = [];
  let cur = '';
  for (const w of words) {
    const t = cur ? `${cur} ${w}` : w;
    if (ctx.measureText(t).width > width && cur) { lines.push(cur); cur = w; } else cur = t;
  }
  if (cur) lines.push(cur);
  if (lines.length > maxLines) {
    lines.length = maxLines;
    let l = lines[maxLines - 1];
    while (l && ctx.measureText(`${l}…`).width > width) l = l.slice(0, -1);
    lines[maxLines - 1] = `${l}…`;
  }
  return lines;
}
function centered(text, font, color, y, width = 340, maxLines = 3, pitch = 0) {
  const lines = String(text).split('\n').flatMap((p) => wrap(p, font, width, maxLines));
  ctx.font = font;
  ctx.fillStyle = color;
  ctx.textAlign = 'center';
  ctx.textBaseline = 'top';
  const lh = pitch || parseInt(font.match(/(\d+)px/)[1], 10) * 1.25;
  lines.forEach((l, i) => ctx.fillText(l, C, y + i * lh));
  return lines.length;
}
// Text along the rim (lv_arclabel in ui.h mk_arc: radius 205, 110° span, top clockwise, bottom counter-clockwise).
// Harness status shimmer (terminal.c:259-268, :1039-1049, :1600-1640; firmware buddy/ui.h shimmer_paint):
// a ±48 px band at full ink crosses the text in 20 × 64 ms, rests 768 ms; everything else at 100/255.
function shimmerAlpha(text, now) {
  const step = Math.floor(now / 64) % 32, phase = 1 + Math.min(step, 20);
  const total = ctx.measureText(text).width, center = -48 + ((total + 96) * (phase - 1)) / 20;
  let pen = 0;
  return [...text].map((ch) => {
    const w = ctx.measureText(ch).width, d = Math.abs(pen + w / 2 - center);
    pen += w;
    const level = d >= 48 ? 0 : 15 - Math.floor((d * 15) / 48);
    return (100 + (level * 155) / 15) / 255;
  });
}
function arcText(text, color, top, shimmer = false, now = 0) {
  if (!text) return;
  ctx.font = '500 26px Inter';
  const alpha = shimmer ? shimmerAlpha(text, now) : null;
  let n = 0;
  ctx.fillStyle = color;
  ctx.textAlign = 'center';
  ctx.textBaseline = 'middle';
  // Harness/firmware: text band ~194-213 px from the centre on both arcs (radius 205 name / 202 status).
  const r = top ? 205 - 8 : 202 + 2;
  const total = ctx.measureText(text).width / r;
  let a = (top ? -Math.PI / 2 : Math.PI / 2) + (top ? -total / 2 : total / 2);
  for (const ch of text) {
    const w = ctx.measureText(ch).width / r;
    a += top ? w / 2 : -w / 2;
    ctx.save();
    ctx.translate(C + r * Math.cos(a), C + r * Math.sin(a));
    ctx.rotate(top ? a + Math.PI / 2 : a - Math.PI / 2);
    ctx.globalAlpha = alpha ? alpha[n] : 1;
    ctx.fillText(ch, 0, 0);
    ctx.globalAlpha = 1;
    n++;
    ctx.restore();
    a += top ? w / 2 : -w / 2;
  }
}
function roundRect(x, y, w, h, r, fill, stroke) {
  ctx.beginPath();
  ctx.roundRect(x, y, w, h, r);
  ctx.fillStyle = fill;
  ctx.fill();
  if (stroke) { ctx.strokeStyle = stroke; ctx.lineWidth = 2; ctx.stroke(); }
}

// ---- state ----
let ui = null;
const clocks = { scene: { key: null, t0: 0 }, alert: { key: null, t0: 0 }, small: { key: null, t0: 0 }, alarm: { key: null, t0: 0 }, usage: { key: null, t0: 0 } };
const since = (c, key, now) => { if (c.key !== key) { c.key = key; c.t0 = now; } return now - c.t0; };
let hits = []; // [{x,y,w,h,target}] for the current frame
// Bridge-connected burst (ui.h burst()): 18 green sparks, centre to ~170-230 px, 700 ms, ease-out cubic.
let burstAt = null, burstR = [], lastView = null;
function drawBurst(now) {
  const t = (now - burstAt) / 700;
  if (t >= 1) return void (burstAt = null);
  const e = 1 - (1 - t) ** 3, d = 14 * (1 - t) + 3;
  ctx.fillStyle = `rgba(0,255,47,${1 - t * t})`;
  burstR.forEach((r, i) => {
    const a = (i * Math.PI * 2) / burstR.length;
    ctx.beginPath();
    ctx.arc(C + Math.cos(a) * r * e, C + Math.sin(a) * r * e, d / 2, 0, Math.PI * 2);
    ctx.fill();
  });
}


// Usage screen (BOOT on the face): one block per provider, a bar per rate window. Bars grow in over 500 ms.
const usageInk = (pct) => (pct >= 95 ? '#cd3131' : pct >= 80 ? '#e5e510' : '#d6d6d2');
function drawUsage(cards, t) {
  const grow = 1 - (1 - Math.min(1, t / 500)) ** 3;
  const NAME = 36, ROW = 32, GAP = 18;
  const total = cards.reduce((h, c) => h + NAME + c.w.length * ROW, 0) + GAP * (cards.length - 1);
  let y = C - total / 2 + 6;
  ctx.textBaseline = 'middle';
  for (const c of cards) {
    ctx.font = '700 26px Inter';
    ctx.fillStyle = '#efe7de';
    ctx.textAlign = 'left';
    ctx.fillText(c.n, 78, y + NAME / 2);
    y += NAME;
    for (const [win, pct, reset] of c.w) {
      const cy = y + ROW / 2;
      ctx.font = '400 19px Inter';
      ctx.fillStyle = '#9a9aa6';
      ctx.textAlign = 'left';
      ctx.fillText(win, 78, cy, 72);
      roundRect(156, cy - 5, 136, 10, 5, '#2a2c34');
      const w = Math.max(10, 136 * Math.min(pct, 100) / 100 * grow);
      roundRect(156, cy - 5, w, 10, 5, usageInk(pct));
      ctx.font = '600 21px Inter';
      ctx.fillStyle = usageInk(pct);
      ctx.textAlign = 'right';
      ctx.fillText(`${Math.round(pct * grow)}%`, 352, cy);
      ctx.font = '400 17px Inter';
      ctx.fillStyle = '#7a7a86';
      ctx.fillText(reset || '', 422, cy, 62);
      y += ROW;
    }
    y += GAP;
  }
}

function frame(now) {
  ctx.fillStyle = '#000';
  ctx.fillRect(0, 0, W, W);
  hits = [];
  if (!ui) {
    centered('waiting for the dial…', '400 25px Inter', '#ada6ad', 220);
    return requestAnimationFrame(frame);
  }
  if (lastView === 'brand' && ui.view !== 'brand') {
    burstAt = now;
    burstR = Array.from({ length: 18 }, () => 170 + Math.random() * 60);
  }
  lastView = ui.view;
  const sc = ui.scene && SCENES[ui.scene];
  const st = since(clocks.scene, ui.scene, now);
  if (sc) drawScene(sc, st, ui.view === 'brand' ? -50 : ui.scene === 'claude_send' ? 0 : 4);
  if (ui.alert && sc) {
    const [ax, ay] = drawAlert(sc, since(clocks.alert, ui.scene + ui.alert, now), ui.alert);
    hits.push({ x: ax - 14, y: ay - 14, w: 110, h: 100, target: 'bell' });
  } else clocks.alert.key = null;

  if (ui.card != null) {
    const lines = wrap(ui.card, '400 30px Inter', 364, 4);
    const h = lines.length * 43 + 42, top = 176 + ((200 - h) >> 1);
    // Small pet above the card (ui.h place_small).
    const z = ui.small || 6, k = Math.floor(since(clocks.small, 'small', now) / pet.step_ms[0]) % pet.steps;
    const [fi, dy] = pet.loops[0][k];
    const im = img(`/${pet.zoom[z]}/${pad(fi)}.png`);
    if (im.complete && im.naturalWidth) ctx.drawImage(im, (W - im.width) >> 1, 66 + ((top - 66 - im.height) >> 1) + Math.trunc(dy * z / 4));
    roundRect(41, top, 384, h, 24, '#1c1e26', '#3d3f47');
    ctx.font = '400 30px Inter';
    ctx.fillStyle = '#d6d6d2';
    ctx.textAlign = 'center';
    ctx.textBaseline = 'top';
    lines.forEach((l, i) => ctx.fillText(l, C, top + 21 + i * 43 + 4));
    hits.push({ x: 41, y: top, w: 384, h, target: 'card' });
  } else clocks.small.key = null;

  if (ui.bell) {
    ctx.font = '400 20px Inter';
    const w = ctx.measureText(ui.bell).width + 26;
    roundRect(C - w / 2, 384, w, 32, 16, '#006fff');
    ctx.fillStyle = '#eaeaf0';
    ctx.textAlign = 'center';
    ctx.textBaseline = 'middle';
    ctx.fillText(ui.bell, C, 400);
    hits.push({ x: C - w / 2 - 20, y: 364, w: w + 40, h: 72, target: 'bell' });
  }
  if (ui.alarm) {
    // ui.h tick(): bounce in/out over the 3 s usage flash, pulsing twice a second. Clock = since it appeared.
    const t = since(clocks.alarm, 'alarm', now);
    const env = Math.max(0, Math.min(1, t / 250, (3000 - t) / 250));
    const k = env * (1 + 0.15 * Math.sin(t * 2 * Math.PI / 500));
    ctx.save();
    ctx.translate(C, C);
    ctx.scale(k, k);
    ctx.font = '800 96px Inter';
    ctx.fillStyle = '#cd3131';
    ctx.textAlign = 'center';
    ctx.textBaseline = 'middle';
    ctx.fillText(ui.alarm, 0, 0);
    ctx.restore();
  } else clocks.alarm.key = null;
  if (ui.word) centered(ui.word, '700 48px Inter', '#efe7de', 262);
  if (ui.sub) centered(ui.sub, '400 25px Inter', '#ada6ad', 270);
  if (ui.perm) {
    centered(ui.perm.title, '400 30px Inter', '#eaeaf0', 132, 330, 1);
    centered(ui.perm.hint, '400 25px Inter', '#d6d6d2', 190, 348, 3);
    if (ui.perm.count) centered(ui.perm.count, '400 20px Inter', '#ada6ad', 290);
    for (const [label, x, w, ink, target] of [['[ deny ]', 83, 156, '#e7a6ad', 'deny'], ['[ allow ]', 243, 140, '#efe7de', 'allow']]) {
      const pressed = press?.target === target;
      if (pressed) roundRect(x, 335, w, 66, 33, '#392c4a');
      ctx.font = '400 25px Inter';
      ctx.fillStyle = pressed ? '#c6aaef' : ink;
      ctx.textAlign = 'center';
      ctx.textBaseline = 'middle';
      ctx.fillText(label, x + w / 2, 368);
      hits.push({ x, y: 335, w, h: 66, target });
    }
  }
  if (ui.view === 'usage' && ui.usage) drawUsage(ui.usage, since(clocks.usage, 'usage', now));
  else clocks.usage.key = null;
  if (ui.list != null) {
    ctx.font = '400 25px Inter';
    ctx.fillStyle = '#efe7de';
    ctx.textAlign = 'left';
    ctx.textBaseline = 'top';
    ui.list.split('\n').filter(Boolean).forEach((l, i) => ctx.fillText(l, 63, 96 + i * 31, 340));
  }
  arcText(ui.name, '#eaeaf0', true);
  arcText(ui.status?.text, ui.status?.color || '#9a9aa6', false, ui.status?.shimmer, now);
  if (ui.asleep) {
    ctx.fillStyle = '#000d';
    ctx.fillRect(0, 0, W, W);
    centered('asleep · tap to wake', '400 20px Inter', '#585863', 222);
  }
  if (burstAt != null) drawBurst(now);
  requestAnimationFrame(frame);
}
requestAnimationFrame(frame);

// ---- input: clicks become the dial's taps (buddy/ui.h tap()) ----
let ws = null, press = null;
function tap(target) {
  if (ws?.readyState === 1) ws.send(JSON.stringify({ type: 'tap', target }));
}
function at(e) {
  const r = canvas.getBoundingClientRect();
  return [(e.clientX - r.left) * W / r.width, (e.clientY - r.top) * W / r.height];
}
canvas.addEventListener('pointerdown', (e) => {
  const [x, y] = at(e);
  const hit = hits.find((h) => x >= h.x && x < h.x + h.w && y >= h.y && y < h.y + h.h);
  press = { x, y, target: hit?.target, held: false };
  press.timer = setTimeout(() => {
    if (!press || ui?.view !== 'face') return;
    press.held = true;
    tap('hold');
  }, 650);
});
canvas.addEventListener('pointerup', (e) => {
  if (!press) return;
  clearTimeout(press.timer);
  const [x] = at(e), p = press;
  press = null;
  if (p.held) return;
  if (ui?.asleep) return tap('wake');
  if (x - p.x > 60) return tap(ui?.view === 'permission' ? 'later' : 'back'); // swipe right
  if (p.target) return tap(p.target);
  if (ui?.view === 'entries') tap('back');
});
canvas.addEventListener('pointerleave', () => { if (press) clearTimeout(press.timer); press = null; });
$('pwr').onclick = () => tap('screen');
$('boot').onclick = () => tap(ui?.view === 'permission' ? 'later' : 'back');

// ---- feed ----
function connect() {
  ws = new WebSocket(`${location.protocol === 'https:' ? 'wss' : 'ws'}://${location.host}/ws`);
  ws.onopen = () => { $('ws').textContent = 'bridge connected'; $('ws').className = 'pill on'; };
  ws.onclose = () => {
    $('ws').textContent = 'bridge offline';
    $('ws').className = 'pill off';
    setTimeout(connect, 2000);
  };
  ws.onmessage = (m) => {
    const msg = JSON.parse(m.data);
    if (msg.type === 'ui') {
      liveUi = msg.ui;
      if (!override) ui = msg.ui;
      $('ui').textContent = JSON.stringify(ui, null, 2);
    } else if (msg.type === 'line') {
      $('line').textContent = JSON.stringify(msg.line, null, 2);
      if (msg.line.usages) lastUsages = msg.line.usages;
    } else if (msg.type === 'dial') {
      $('dial').textContent = msg.ready ? `dial ${msg.host}` : 'dial offline';
      $('dial').className = `pill ${msg.ready ? 'on' : 'off'}`;
    }
  };
}
connect();

// ---- previews: draw a state locally to approve UI changes before they reach the firmware ----
// override replaces the dial's ui until it expires; then the live ui comes back (and transitions play, e.g. burst).
let override = null;
let lastUsages = [{ n: 'Claude', w: [['5h', 42, '2h 8m'], ['7d', 83, '4d']] }, { n: 'Copilot', w: [['premium', 24, '12d']] },
                  { n: 'Grok', w: [['7d', 97, '4d']] }];
const PREVIEWS = {
  'usage screen (BOOT)': () => ({
    ms: 8000,
    ui: { v: 1, view: 'usage', asleep: false, name: 'Usage', usage: lastUsages,
          status: { text: 'BOOT to close', color: '#9a9aa6' } },
  }),
  'splash → connect': () => ({
    ms: 3000,
    ui: { v: 1, view: 'brand', asleep: false, name: '', word: 'Claude', scene: 'claude_relax',
          status: { text: 'Looking for the bridge', color: '#ada6ad', shimmer: true } },
  }),
  'usage alarm': () => ({
    ms: 3000,
    ui: { ...(liveUi || {}), view: 'face', scene: undefined, alarm: '87%',
          status: { text: 'Claude 5h 87% · 7d 40% · 1h 2m', color: '#e5e510' } },
  }),
};
let liveUi = null;
for (const [name, make] of Object.entries(PREVIEWS)) {
  const b = document.createElement('button');
  b.textContent = name;
  b.onclick = () => {
    const p = make();
    liveUi = liveUi || ui;
    override = { until: performance.now() + p.ms };
    ui = p.ui;
    setTimeout(() => { override = null; ui = liveUi; }, p.ms);
  };
  $('previews').append(b);
}

if (import.meta.hot) import.meta.hot.accept(() => location.reload());
