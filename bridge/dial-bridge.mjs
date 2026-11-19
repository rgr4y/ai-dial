#!/usr/bin/env node
// dial-bridge: agent hook events -> Claude Buddy dial, over the ESPHome native API.
//
//   hook.sh (Claude Code hooks, peon-ping adapters) --HTTP--> this daemon --ESPHome API--> dial
//   dial [ allow ] / [ deny ] --text_sensor buddy_decision--> this daemon --> the waiting PermissionRequest hook
//
// HTTP on 127.0.0.1:${DIAL_BRIDGE_PORT:-19876}:
//   POST /event       any hook JSON; answers at once
//   POST /permission  PermissionRequest hook JSON; held open until the dial answers -> {"decision":"allow"|"deny"},
//                     or 204 when the dial is offline, the prompt was answered elsewhere, or it timed out.
//   GET  /status      daemon + dial state, for humans
// HTTP on 0.0.0.0:${DIAL_ASSETS_PORT:-19877}, read-only: GET /<character>/<file> from bridge/assets (character packs
// made by scripts/gen_buddy_pets.mjs). Heartbeats carry {"assets":{"url","rev"}}; the dial syncs LittleFS when rev
// changes. Nothing else listens beyond loopback.
// Fail-open everywhere: with no daemon or no dial, Claude Code's own prompt works exactly as before.
//
// Several dials can be driven at once (the dial and the desktop sim, claude-buddy-sim.yaml): DIAL_HOST is a comma list;
// all get the same heartbeats, the first to answer a prompt wins.
// Config (env): DIAL_HOST (10.0.0.11, reserved on the router; e.g. "10.0.0.11,127.0.0.1"), DIAL_KEY (API encryption key; default: api_key from
// DIAL_SECRETS, ~/workspace/esphome/secrets.yaml), DIAL_BRIDGE_PORT (19876), DIAL_PROMPT_TIMEOUT_S (280).

import { execFile } from 'node:child_process';
import { createReadStream, readFileSync, statSync } from 'node:fs';
import { createServer } from 'node:http';
import { homedir } from 'node:os';
import { join, normalize, sep } from 'node:path';
import esphome from '@2colors/esphome-native-api';
import { Board } from './board.mjs';

const { Client, ServiceArgType } = esphome;
const HOSTS = (process.env.DIAL_HOST || '10.0.0.11').split(',').map((h) => h.trim()).filter(Boolean);  // IPs skip mDNS
const PORT = Number(process.env.DIAL_BRIDGE_PORT || 19876);
const PROMPT_TIMEOUT_MS = Number(process.env.DIAL_PROMPT_TIMEOUT_S || 280) * 1000;
const KEEPALIVE_MS = 10000; // the dial treats 30 s of silence as gone

function apiKey() {
  if (process.env.DIAL_KEY) return process.env.DIAL_KEY;
  const path = process.env.DIAL_SECRETS || `${homedir()}/workspace/esphome/secrets.yaml`;
  const m = readFileSync(path, 'utf8').match(/^api_key:\s*["']?([A-Za-z0-9+/=]+)["']?\s*$/m);
  if (!m) throw new Error(`no api_key in ${path}`);
  return m[1];
}

const log = (...a) => console.log(new Date().toISOString().slice(11, 19), ...a);
const board = new Board();
const waiters = new Map(); // prompt id -> resolve(decision | null)

// ---- dial links ----
// One per host: { host, client, keys, ready, ui }.
const dials = HOSTS.map((host) => ({ host, client: null, lineKey: null, tapKey: null, decisionKey: null, uiKey: null,
                                     ready: false, ui: null, lastErr: null }));
const anyReady = () => dials.some((d) => d.ready);

function connectDial(dial) {
  const client = new Client({
    host: dial.host,
    port: 6053,
    encryptionKey: apiKey(),
    clientInfo: 'dial-bridge',
    initializeDeviceInfo: true,
    initializeListEntities: true,
    initializeSubscribeStates: true,
    reconnect: true,
    reconnectInterval: 1000,  // the dial boots to Wi-Fi in a few seconds; don't add 5 s on top
  });
  dial.client = client;
  client.connection.on('message.ListEntitiesServicesResponse', (m) => {
    if (m.name === 'buddy_line') dial.lineKey = m.key;
    if (m.name === 'buddy_tap') dial.tapKey = m.key;
  });
  client.connection.on('message.ListEntitiesTextSensorResponse', (m) => {
    // Match on name too: recent ESPHome leaves object_id empty in ListEntities.
    if (m.objectId === 'buddy_decision' || m.name === 'Buddy Decision') dial.decisionKey = m.key;
    if (m.objectId === 'buddy_ui' || m.name === 'Buddy UI') dial.uiKey = m.key;
  });
  client.connection.on('message.TextSensorStateResponse', (m) => {
    if (m.key === dial.uiKey && m.state) {
      try {
        dial.ui = JSON.parse(m.state);
        onUiForUsage();
      } catch {}
      return;
    }
    if (m.key !== dial.decisionKey || !m.state) return;
    const [id, decision] = m.state.split(' ');
    const resolve = waiters.get(id);
    if (!resolve) return; // a decision from before this connection (state replay on subscribe)
    log(`dial ${dial.host} answered`, id, decision);
    resolve(decision === 'once' ? 'allow' : 'deny');
  });
  client.on('initialized', () => {
    dial.ready = !!dial.lineKey;
    dial.lastErr = null;
    log(dial.ready ? `dial connected (${dial.host})` : `dial ${dial.host} has no buddy_line action: old firmware?`);
    push();
  });
  client.on('disconnected', () => {
    if (dial.ready) log(`dial disconnected (${dial.host})`);
    dial.ready = false;
    dial.ui = null;
    onUiForUsage();
  });
  client.on('error', (e) => {
    if (e.message !== dial.lastErr) log(`dial ${dial.host} error:`, e.message);  // once, not every reconnect
    dial.lastErr = e.message;
  });
  client.connect();
}

// obj: a line, or (dial) => line when it differs per dial (heartbeats carry the asset URL each dial reaches us at).
function sendLine(obj) {
  let sent = false;
  for (const dial of dials) {
    if (!dial.ready) continue;
    try {
      dial.client.connection.executeServiceService({
        key: dial.lineKey,
        args: [{ type: ServiceArgType.String, value: JSON.stringify(typeof obj === 'function' ? obj(dial) : obj) }],
      });
      sent = true;
    } catch (e) {
      log(`send to ${dial.host} failed:`, e.message);
    }
  }
  return sent;
}

// ---- plan usage ----
// Primary: CodexBar's HTTP server (GET /usage, loopback needs no token): one payload per enabled provider.
// Fallback: ~/.claude/refresh-usage.sh (Claude OAuth usage API, TTL 150 s) writes ~/.claude/stats-usage.json.
// The dial flashes `usage` on the lower arc 3 s of every 15 s; after each flash we rotate to the next provider.
const CODEXBAR_URL = process.env.CODEXBAR_URL || 'http://127.0.0.1:6969/usage';
const USAGE_CACHE = `${homedir()}/.claude/stats-usage.json`;
const USAGE_SCRIPT = `${homedir()}/.claude/refresh-usage.sh`;
let usages = []; // [{ text, pct, card }]; card = { n, w: [[window, pct, reset]] } for the dial's usage screen
let usageIdx = 0;
let usage = null; // the one currently sent

function leftText(iso) {
  const left = Date.parse(iso) - Date.now();
  if (!(left > 0)) return '';
  const h = Math.floor(left / 3.6e6);
  return h >= 48 ? `${Math.round(h / 24)}d` : `${h}h ${Math.floor((left % 3.6e6) / 6e4)}m`;
}
const untilText = (iso) => (leftText(iso) ? ` · ${leftText(iso)}` : '');
function windowName(w, label) {
  if (w.windowMinutes === 300) return '5h';
  if (w.windowMinutes === 10080) return '7d';
  return (label || '').toLowerCase();
}
function setUsages(list) {
  usages = list;
  const next = usages[usageIdx % usages.length] || null;
  if (next?.text !== usage?.text) {
    usage = next;
    push();
  }
}
// The dial just showed `usage`; switch to the next provider before the next flash.
// The usage screen (BOOT) asks for every provider just by publishing view "usage": while it's open, heartbeats carry
// `usages`; opening it pushes right away and refreshes CodexBar.
let rotateTimer = null, usageScreen = false;
function onUiForUsage() {
  const open = dials.some((d) => d.ui?.view === 'usage');  // any dial on its usage screen
  if (open !== usageScreen) {
    usageScreen = open;
    if (open) refreshUsage();
    push();
  }
  if (usages.length < 2 || rotateTimer || !usage || !dials.some((d) => d.ui?.status?.text === usage.text)) return;
  rotateTimer = setTimeout(() => {
    rotateTimer = null;
    usageIdx = (usageIdx + 1) % usages.length;
    setUsages(usages);
  }, 4000);
}

async function fromCodexBar() {
  const r = await fetch(CODEXBAR_URL, { signal: AbortSignal.timeout(40000) });
  if (!r.ok) throw new Error(`codexbar ${r.status}`);
  const list = [];
  for (const p of await r.json()) {
    const u = p.usage, labels = p.rateWindowLabels || {};
    if (p.error || !u?.primary) continue;
    const wins = [['primary', u.primary], ['secondary', u.secondary]].filter(([, w]) => w);
    // One window (Copilot's "Premium"): the provider name says enough.
    const label = (k, w) => (wins.length > 1 ? windowName(w, labels[k]) : '');
    const parts = wins.map(([k, w]) => `${label(k, w)} ${Math.round(w.usedPercent)}%`.trim());
    const name = p.provider[0].toUpperCase() + p.provider.slice(1);
    list.push({
      text: `${name} ${parts.join(' · ')}${untilText(u.primary.resetsAt)}`,
      pct: Math.round(Math.max(...wins.map(([, w]) => w.usedPercent))),
      card: { n: name, w: wins.map(([k, w]) => [label(k, w), Math.round(w.usedPercent), leftText(w.resetsAt)]) },
    });
  }
  return list;
}
function fromClaudeScript() {
  execFile(USAGE_SCRIPT, { timeout: 15000 }, () => {
    try {
      const u = JSON.parse(readFileSync(USAGE_CACHE, 'utf8'));
      const h5 = Math.round(u.five_hour?.utilization ?? 0), d7 = Math.round(u.seven_day?.utilization ?? 0);
      setUsages([{ text: `5h ${h5}% · 7d ${d7}%${untilText(u.five_hour?.resets_at)}`, pct: Math.max(h5, d7),
                   card: { n: 'Claude', w: [['5h', h5, leftText(u.five_hour?.resets_at)], ['7d', d7, leftText(u.seven_day?.resets_at)]] } }]);
    } catch {}
  });
}
async function refreshUsage() {
  try {
    const list = await fromCodexBar();
    if (list.length) return setUsages(list);
  } catch (e) {
    log('codexbar usage:', e.message);
  }
  fromClaudeScript();
}
refreshUsage();
setInterval(refreshUsage, 60000);

// ---- character packs (LAN, read-only) ----
const ASSETS_PORT = Number(process.env.DIAL_ASSETS_PORT || 19877);
const ASSETS_DIR = join(import.meta.dirname, 'assets');
const CHARACTER = 'claude';
let manifestCache = { mtime: 0, rev: null };
function assetsLine(dial) {
  // The address the dial sees us at: our end of the API connection.
  const local = dial?.client?.connection?.frameHelper?.socket?.localAddress?.replace(/^::ffff:/, '');
  if (!local) return null;
  try {
    const st = statSync(join(ASSETS_DIR, CHARACTER, 'manifest.json'));
    if (st.mtimeMs !== manifestCache.mtime)
      manifestCache = { mtime: st.mtimeMs, rev: JSON.parse(readFileSync(join(ASSETS_DIR, CHARACTER, 'manifest.json'), 'utf8')).rev };
  } catch {
    return null;
  }
  return { url: `http://${local}:${ASSETS_PORT}/${CHARACTER}/`, rev: manifestCache.rev };
}
createServer((req, res) => {
  const path = normalize(decodeURIComponent(new URL(req.url, 'http://x').pathname));
  const file = join(ASSETS_DIR, path);
  if (req.method !== 'GET' || !file.startsWith(ASSETS_DIR + sep)) return res.writeHead(404).end();
  let st;
  try {
    st = statSync(file);
  } catch {
    return res.writeHead(404).end();
  }
  if (!st.isFile()) return res.writeHead(404).end();
  log('assets', req.socket.remoteAddress, path);
  res.writeHead(200, { 'content-length': st.size, 'content-type': 'application/octet-stream' });
  createReadStream(file).pipe(res);
}).listen(ASSETS_PORT, '0.0.0.0', () => log(`assets on 0.0.0.0:${ASSETS_PORT} from ${ASSETS_DIR}`));

function heartbeat(dial) {
  const hb = board.heartbeat(Date.now());
  const assets = assetsLine(dial);
  if (assets) hb.assets = assets;
  if (usage) Object.assign(hb, { usage: usage.text, usage_pct: usage.pct, ...(usageScreen && { usages: usages.map((u) => u.card) }) });
  return hb;
}

let pushTimer = null;
function push() {
  // Coalesce bursts (PreToolUse/PostToolUse pairs) into one heartbeat.
  clearTimeout(pushTimer);
  pushTimer = setTimeout(() => sendLine(heartbeat), 150);
}
setInterval(() => sendLine(heartbeat), KEEPALIVE_MS);

// ---- HTTP ----
function body(req) {
  return new Promise((resolve) => {
    let b = '';
    req.on('data', (c) => { b += c; if (b.length > 1e6) req.destroy(); });
    req.on('end', () => { try { resolve(JSON.parse(b)); } catch { resolve(null); } });
  });
}

function onEvent(ev) {
  const r = board.event(ev, Date.now());
  if (r.cleared && waiters.has(r.cleared)) waiters.get(r.cleared)(null);
  if (r.turn) sendLine({ evt: 'turn', role: 'assistant', content: [{ type: 'text', text: r.turn }] });
  push();
}

async function onPermission(ev, res) {
  if (!anyReady() || !ev?.session_id) return res.writeHead(204).end(); // no dial online: terminal prompt only
  const id = board.openPrompt(ev, Date.now());
  log('prompt', id, ev.tool_name);
  push();
  const decision = await new Promise((resolve) => {
    const timer = setTimeout(() => resolve(null), PROMPT_TIMEOUT_MS);
    waiters.set(id, (d) => { clearTimeout(timer); resolve(d); });
    res.on('close', () => resolve(null)); // hook killed (answered in the terminal, or Claude moved on)
  });
  waiters.delete(id);
  board.closePrompt(id);
  push();
  if (res.writableEnded || res.destroyed) return;
  if (!decision) return res.writeHead(204).end();
  res.writeHead(200, { 'content-type': 'application/json' }).end(JSON.stringify({ decision }));
}

const server = createServer(async (req, res) => {
  if (req.method === 'GET' && req.url === '/status') {
    return res.writeHead(200, { 'content-type': 'application/json' })
      .end(JSON.stringify({ dials: dials.map((d) => ({ host: d.host, ready: d.ready, view: d.ui?.view })),
                            heartbeat: heartbeat(dials.find((d) => d.ready)) }, null, 2));
  }
  if (req.method !== 'POST') return res.writeHead(404).end();
  const ev = await body(req);
  if (req.url === '/permission') return onPermission(ev, res);
  if (req.url === '/event') {
    if (ev) onEvent(ev);
    return res.writeHead(204).end();
  }
  res.writeHead(404).end();
});
server.listen(PORT, '127.0.0.1', () => log(`listening on 127.0.0.1:${PORT}, dials ${HOSTS.join(', ')}`));

dials.forEach(connectDial);
