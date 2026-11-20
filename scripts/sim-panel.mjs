#!/usr/bin/env node
// Sim control panel: a small page (http://localhost:19880) that pins the desktop sim to a test state by writing
// build/sim-pin.jsonl (claude-buddy-sim.yaml; bridge/README.md "Pin a scenario"). Release deletes the file and the
// bridge takes the face back.
//   node scripts/sim-panel.mjs   (or make sim-panel)
// Bots: the live roster from the bridge's /status, else every shape in art/grok in white.
import { createServer } from 'node:http';
import { readFileSync, readdirSync, rmSync, writeFileSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const PIN = join(ROOT, 'build/sim-pin.jsonl');
const PORT = 19880, BRIDGE = 'http://localhost:19876/status';

async function bots() {
  try {
    const r = await fetch(BRIDGE, { signal: AbortSignal.timeout(1000) });
    const b = (await r.json()).heartbeat?.bots;
    if (b?.length) return b;
  } catch {}
  return readdirSync(join(ROOT, 'art/grok')).filter((f) => f.endsWith('.svg')).map((f) => [f.slice(0, -4), 0xffffff, f.slice(0, -4)]);
}

// Three working sessions, two named alike (the second is a Codex one): swipe, dots, "auto-ai ·2".
const SESSIONS = [{ id: 's1', name: 'auto-ai', tool: 'Edit' }, { id: 's2', name: 'auto-ai', tool: 'Bash', agent: 'codex' },
                  { id: 's3', name: 'api', tool: '' }];

// mode: work (a busy session shown as the bot), idle (no sessions; the pinned roster is just this bot), multi (the
// three sessions above), notice (multi, plus a finished reply from s2: the bell) or reply (idle with a reply card from
// s1, so holding the face has a session to talk to). Voice itself needs no pin: hold the face in the sim.
function pin(mode, [shape, color, name]) {
  const lines = [];
  const hb = mode === 'multi' || mode === 'notice'
    ? { total: 3, running: 3, waiting: 0, msg: 'auto-ai Edit', entries: [], work: SESSIONS, latest: 's1' }
    : mode === 'work'
    ? { total: 1, running: 1, waiting: 0, msg: `${name} working`, entries: [], avatar: shape, avatar_color: color, avatar_name: name }
    : { total: 0, running: 0, waiting: 0, msg: '', entries: [] };
  lines.push({ ...hb, bots: [[shape, color, name]] });
  if (mode === 'reply') lines.push({ evt: 'turn', role: 'assistant', content: [{ type: 'text', text: 'auto-ai: All tests pass. Anything else?' }], sid: 's1' });
  if (mode === 'notice') lines.push({ evt: 'turn', role: 'assistant', content: [{ type: 'text', text: 'auto-ai: All tests pass.' }], sid: 's2', busy: 2, agent: 'codex' });
  writeFileSync(PIN, lines.map((l) => JSON.stringify(l)).join('\n') + '\n');
  console.log(`pin ${mode} ${name} (${shape})`);
}

const PAGE = `<!doctype html><meta charset="utf-8"><title>Sim Panel</title>
<style>
body{font:14px -apple-system,sans-serif;background:#1b1b1f;color:#ddd;margin:16px;width:260px}
h2{font-size:12px;text-transform:uppercase;color:#888;margin:18px 0 6px}
button{font:inherit;padding:8px 12px;margin:2px 4px 2px 0;border:1px solid #444;border-radius:6px;background:#2a2a30;color:#eee;cursor:pointer}
button.on{border-color:#d97757;background:#3a2a24}
#bot{font-size:16px;margin:6px 0}#sw{display:inline-block;width:12px;height:12px;border-radius:3px;margin-right:6px;vertical-align:-1px}
</style>
<h2>Bots</h2><button onclick="step(-1)">◀ Prev</button><button onclick="step(1)">Next bot ▶</button>
<h2>Current Bot</h2><div id="bot"></div>
<button id="work" onclick="set('work')">Working</button><button id="idle" onclick="set('idle')">Idle</button>
<h2>Sessions</h2><button id="multi" onclick="set('multi')">3 sessions</button><button id="notice" onclick="set('notice')">+ reply</button><button id="reply" onclick="set('reply')">Reply card</button>
<h2>Sim</h2><button onclick="release()">Release to bridge</button>
<script>
let bots = [], i = 0, mode = null;
const hex = (c) => '#' + c.toString(16).padStart(6, '0');
function show() {
  const [shape, c, name] = bots[i];
  document.getElementById('bot').innerHTML = '<span id="sw" style="background:' + hex(c) + '"></span>' + name + ' <small style="color:#888">' + shape + ' · ' + (i + 1) + '/' + bots.length + '</small>';
  for (const m of ['work', 'idle', 'multi', 'notice', 'reply']) document.getElementById(m).className = m === mode ? 'on' : '';
}
async function send() { if (mode) await fetch('/pin', { method: 'POST', body: JSON.stringify({ mode, bot: bots[i] }) }); show(); }
function step(d) { i = (i + d + bots.length) % bots.length; mode ??= 'idle'; send(); }  // Next shows it: idle unless working
function set(m) { mode = m; send(); }
async function release() { mode = null; await fetch('/release', { method: 'POST' }); show(); }
// Start from what the sim is pinned to now (a reload, or a pin written by hand), else the first bot, unpinned.
Promise.all([fetch('/bots').then((r) => r.json()), fetch('/pin').then((r) => r.json())]).then(([b, p]) => {
  bots = b;
  if (p) {
    const k = bots.findIndex((x) => x[2] === p.bot[2] && x[0] === p.bot[0]);
    if (k >= 0) i = k; else { bots.unshift(p.bot); i = 0; }
    mode = p.mode;
  }
  show();
});
</script>`;

createServer(async (req, res) => {
  let body = '';
  for await (const c of req) body += c;
  if (req.url === '/bots') return res.end(JSON.stringify(await bots()));
  if (req.url === '/pin' && req.method === 'GET') {
    let p = null;
    try {
      const hb = JSON.parse(readFileSync(PIN, 'utf8').split('\n')[0]);
      const reply = readFileSync(PIN, 'utf8').includes('"evt":"turn"') && !hb.work;
      p = { mode: hb.work ? 'multi' : hb.running ? 'work' : reply ? 'reply' : 'idle', bot: hb.bots?.[0] };  // a notice pin reads back as multi
    } catch {}
    return res.end(JSON.stringify(p?.bot ? p : null));
  }
  if (req.url === '/pin' && req.method === 'POST') {
    const { mode, bot } = JSON.parse(body);
    pin(mode, bot);
    return res.end('ok');
  }
  if (req.url === '/release' && req.method === 'POST') {
    rmSync(PIN, { force: true });
    console.log('released to bridge');
    return res.end('ok');
  }
  res.setHeader('content-type', 'text/html');
  res.end(PAGE);
}).listen(PORT, '127.0.0.1', () => console.log(`sim panel: http://localhost:${PORT}`));
