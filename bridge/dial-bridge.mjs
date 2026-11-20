#!/usr/bin/env node
// dial-bridge: agent hook events -> Claude Buddy dial, over the ESPHome native API.
//
//   hook.sh (Claude Code hooks, peon-ping adapters) --HTTP--> this daemon --ESPHome API--> dial
//   dial Allow / Deny --text_sensor buddy_decision--> this daemon --> the waiting PermissionRequest hook
//
// HTTP on 127.0.0.1:${DIAL_BRIDGE_PORT:-19876}:
//   POST /event       any hook JSON; answers at once
//   POST /permission  PermissionRequest hook JSON; held open until the dial answers -> {"decision":"allow"|"deny"},
//                     or 204 when the dial is offline, the prompt was answered elsewhere, or it timed out.
//   POST /speak       {"text"}: say it on the dial speaker (rules -> Wyoming TTS -> announcement); {"played":bool} when done
//   GET  /status      daemon + dial state, for humans
// HTTP on 0.0.0.0:${DIAL_ASSETS_PORT:-19877}, read-only: GET /<character>/<file> from bridge/assets (character packs
// made by scripts/gen_buddy_pets.mjs). Heartbeats carry {"assets":{"url","rev"}}; the dial syncs LittleFS when rev
// changes. Nothing else listens beyond loopback.
// Fail-open everywhere: with no daemon or no dial, Claude Code's own prompt works exactly as before.
//
// Several dials can be driven at once (the dial and the desktop sim, claude-buddy-sim.yaml): DIAL_HOST is a comma list;
// all get the same heartbeats, the first to answer a prompt wins.
// Config (env): DIAL_HOST (10.0.1.46, static in claude-buddy.yaml; e.g. "10.0.1.46,127.0.0.1"), DIAL_KEY (API encryption key; default: api_key from
// DIAL_SECRETS, ~/workspace/esphome/secrets.yaml), DIAL_BRIDGE_PORT (19876), DIAL_PROMPT_TIMEOUT_S (280),
// GROK_BOT_STORE (Grok Bot's store dir, watched read-only; skipped if absent), VOICE_STT_HOST / VOICE_STT_PORT
// (Wyoming STT, 127.0.0.1:10300), VOICE_TTS_HOST / VOICE_TTS_PORT (Wyoming TTS, 127.0.0.1:10200).
//
// Voice: the dial's voice_assistant streams mic audio to this daemon (the API client that subscribed), which relays it
// to a Wyoming STT server, sends the words back as {"evt":"voice"} lines and, when the dial says "send"
// (buddy_ui voice), types the text into the iTerm session of the dial's sid. router.mjs first decides: name rules, then
// one JSON call through the proxy at ANTHROPIC_BASE_URL (key ANTHROPIC_API_KEY, model VOICE_ROUTER_MODEL, default
// gemini-3-flash): a named session, a prompt answer, or the assistant (tmux session VOICE_ASSISTANT_SESSION, "voice").
// See docs/DIAL-PROTOCOL.md "Voice". VOICE_ROUTER_TIMEOUT_MS (3000).

import { execFile } from 'node:child_process';
import { closeSync, createReadStream, existsSync, openSync, readdirSync, readFileSync, readSync, statSync, watch } from 'node:fs';
import { createServer } from 'node:http';
import { homedir } from 'node:os';
import { join, normalize, sep } from 'node:path';
import esphome from '@2colors/esphome-native-api';
import { Board } from './board.mjs';
import { botLooks, conversationId, decodeKey, eventsSince, readRoster } from './grokbot.mjs';
import { chat, route, sessionList } from './router.mjs';
import { createVoiceFlow } from './voiceflow.mjs';
import { cleanText, openStt, registerVoiceMessages, stashTts, synthesize, tmuxHasSession, ttsFiles, typeIntoTmuxSession, typeIntoTty, wavMs } from './voice.mjs';

const { Client, ServiceArgType } = esphome;
const HOSTS = (process.env.DIAL_HOST || '10.0.1.46').split(',').map((h) => h.trim()).filter(Boolean);  // IPs skip mDNS
const PORT = Number(process.env.DIAL_BRIDGE_PORT || 19876);
const PROMPT_TIMEOUT_MS = Number(process.env.DIAL_PROMPT_TIMEOUT_S || 280) * 1000;
const GROK_STORE = process.env.GROK_BOT_STORE || `${homedir()}/Library/Application Support/Grok Bot/sand-client-persistence`;
const STT_HOST = process.env.VOICE_STT_HOST || '127.0.0.1';
const STT_PORT = Number(process.env.VOICE_STT_PORT || 10300);
const TTS_HOST = process.env.VOICE_TTS_HOST || '127.0.0.1';
const TTS_PORT = Number(process.env.VOICE_TTS_PORT || 10200);
const ROUTER_TIMEOUT_MS = Number(process.env.VOICE_ROUTER_TIMEOUT_MS || 3000);
const ROUTER_ENV = { apiKey: process.env.ANTHROPIC_API_KEY, baseUrl: process.env.ANTHROPIC_BASE_URL };  // key from env only; none = no model call
const ASSISTANT = process.env.VOICE_ASSISTANT_SESSION || 'voice';  // tmux session running the voice assistant Claude (make voice)
const pb = registerVoiceMessages();  // before any connection: the API library lacks the voice message ids
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
                                     ready: false, ui: null, lastErr: null, stt: null, pending: null, ask: null, playerKey: null, playerState: null, onPlayer: null }));
const anyReady = () => dials.some((d) => d.ready);
// The address the dial reaches us at: our end of its API connection.
function localAddr(dial) {
  return dial?.client?.connection?.frameHelper?.socket?.localAddress?.replace(/^::ffff:/, '');
}

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
  client.connection.on('message.ListEntitiesMediaPlayerResponse', (m) => { dial.playerKey ??= m.key; });
  client.connection.on('message.MediaPlayerStateResponse', (m) => {
    if (m.key !== dial.playerKey) return;
    dial.playerState = m.state;
    dial.onPlayer?.(m.state);
  });
  client.connection.on('message.TextSensorStateResponse', (m) => {
    if (m.key === dial.uiKey && m.state) {
      try {
        dial.ui = JSON.parse(m.state);
        onUiForUsage();
        onUiForVoice(dial);
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
  client.connection.on('message.VoiceAssistantRequest', (m) => (m.start ? voiceStart(dial) : voiceStop(dial)));
  client.connection.on('message.VoiceAssistantAudio', (m) => { if (m.data) dial.stt?.audio(Buffer.from(m.data, 'base64')); });
  client.on('initialized', () => {
    try {
      const sub = new pb.SubscribeVoiceAssistantRequest();  // one subscriber only: HA doing the same would lock us out
      sub.setSubscribe(true);
      sub.setFlags(0);
      client.connection.sendMessage(sub);
    } catch (e) {
      log(`voice subscribe on ${dial.host} failed:`, e.message);
    }
    dial.ready = !!dial.lineKey;
    dial.lastErr = null;
    log(dial.ready ? `dial connected (${dial.host})` : `dial ${dial.host} has no buddy_line action: old firmware?`);
    push();
  });
  client.on('disconnected', () => {
    if (dial.ready) log(`dial disconnected (${dial.host})`);
    dial.ready = false;
    dial.ui = null;
    dial.playerState = null;
    dial.onPlayer?.(null);
    dial.stt?.abort();
    dial.stt = dial.pending = dial.ask = null;
    onUiForUsage();
  });
  client.on('error', (e) => {
    if (e.message !== dial.lastErr) log(`dial ${dial.host} error:`, e.message);  // once, not every reconnect
    dial.lastErr = e.message;
  });
  client.connect();
}

// obj: a line, or (dial) => line when it differs per dial (heartbeats carry the asset URL each dial reaches us at).
// only: one dial (voice lines go back to the dial that spoke).
function sendLine(obj, only) {
  let sent = false;
  for (const dial of only ? [only] : dials) {
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

// ---- voice ----
// The dial holds its face -> voice_assistant.start -> VoiceAssistantRequest(start) here; audio chunks follow as
// VoiceAssistantAudio; release -> VoiceAssistantRequest(stop). The dial's buddy_ui carries voice (listening, wait,
// confirm, send, note) and sid (the session it talks to, fixed when the hold began). Text only goes out on "send", and
// only to a session whose terminal we know: no tty, no typing.
const say = (dial, o) => sendLine({ evt: 'voice', ...o }, dial);
const PLAYING = pb.MediaPlayerState.MEDIA_PLAYER_STATE_PLAYING;  // the API reports an announcement as PLAYING, never ANNOUNCING (esphome api_connection.cpp try_send_media_player_state)
// The flow itself (hold -> STT -> route -> confirm -> send, the session question, the speak queue) is voiceflow.mjs;
// this wires in the board, router, iTerm, Wyoming and the protobuf messages. Speaking: text -> claude-speak rules ->
// Wyoming TTS (WAV) -> served at tts/<id>.wav on the assets port -> media_player announcement on the dial.
const { voiceStart, voiceStop, speak, onUiForVoice, takeTalked } = createVoiceFlow({
  dials, board, say, log, PLAYING, sessionList, route, routerEnv: ROUTER_ENV, routerTimeoutMs: ROUTER_TIMEOUT_MS,
  openStt: (onPartial) => openStt({ host: STT_HOST, port: STT_PORT, onPartial }),
  cleanText, answerPrompt, typeIntoTty, typeIntoTmuxSession, tmuxHasSession, assistant: ASSISTANT,
  chat: (text) => chat({ text, env: ROUTER_ENV }),
  synthesize: (text) => synthesize(text, { host: TTS_HOST, port: TTS_PORT }), stashTts, wavMs,
  ttsUrl: (addr, id) => `http://${addr}:${ASSETS_PORT}/tts/${id}.wav`,
  localAddress: localAddr,
  startResponse: (dial) => {
    const res = new pb.VoiceAssistantResponse();  // port 0: audio comes over the API connection
    res.setPort(0);
    res.setError(false);
    dial.client.connection.sendMessage(res);
  },
  announce: (dial, url) => {
    const m = new pb.MediaPlayerCommandRequest();  // the library's MediaPlayer entity has no announcement flag; the message does
    m.setKey(dial.playerKey);
    m.setHasMediaUrl(true);
    m.setMediaUrl(url);
    m.setHasAnnouncement(true);
    m.setAnnouncement(true);
    dial.client.connection.sendCommandMessage(m);
  },
});
// Only if that very prompt is still open (answered in the terminal during the 2 s confirm = nothing to do).
function answerPrompt(sid, promptId, decision) {
  const w = promptId && board.sessions.get(sid)?.prompt?.id === promptId && waiters.get(promptId);
  if (!w) return log(`voice: prompt for ${sid} is gone, ${decision} dropped`);
  log(`voice: ${decision} prompt ${promptId}`);
  w(decision);
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
      setUsages([{ text: `Claude 5h ${h5}% · 7d ${d7}%${untilText(u.five_hour?.resets_at)}`, pct: Math.max(h5, d7),
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
function assetsLine(dial) {
  const local = localAddr(dial);
  if (!local) return null;
  try {
    const { rev } = JSON.parse(readFileSync(join(ASSETS_DIR, CHARACTER, 'manifest.json'), 'utf8'));
    return { url: `http://${local}:${ASSETS_PORT}/${CHARACTER}/`, rev };
  } catch {
    return null;
  }
}
createServer((req, res) => {
  const tts = req.method === 'GET' && new URL(req.url, 'http://x').pathname.match(/^\/tts\/([0-9a-f]{16})\.wav$/);
  if (tts) {
    const wav = ttsFiles.get(tts[1]);
    if (!wav) return res.writeHead(404).end();
    log('assets', req.socket.remoteAddress, `tts/${tts[1]}.wav`);
    return res.writeHead(200, { 'content-length': wav.length, 'content-type': 'audio/wav' }).end(wav);
  }
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
  if (grokBots.length) hb.bots = grokBots;
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

// A Claude Code session's /rename title: the transcript re-appends {"type":"custom-title","customTitle":...} every
// few dozen lines, so the last 256 KB holds the current one. Read at most every 10 s per transcript; only files
// under ~/.claude/projects (the hook payload is local, but it still names the path).
const TITLE_TAIL = 256 * 1024, TITLE_EVERY_MS = 10000, titles = new Map();  // path -> { name, at }
const PROJECTS = join(homedir(), '.claude', 'projects') + sep;
function sessionName(path) {
  if (typeof path !== 'string' || !normalize(path).startsWith(PROJECTS) || !path.endsWith('.jsonl')) return undefined;
  const c = titles.get(path);
  if (c && Date.now() - c.at < TITLE_EVERY_MS) return c.name;
  let name;
  try {
    const fd = openSync(path, 'r');
    const size = statSync(path).size, n = Math.min(size, TITLE_TAIL), buf = Buffer.alloc(n);
    readSync(fd, buf, 0, n, size - n);
    closeSync(fd);
    const all = [...buf.toString('utf8').matchAll(/"customTitle":("(?:[^"\\]|\\.)*")/g)];
    if (all.length) name = JSON.parse(all.at(-1)[1]) || undefined;
  } catch {}
  titles.set(path, { name, at: Date.now() });
  return name;
}

function onEvent(ev) {
  if (!ev.agent) ev.session_name ??= sessionName(ev.transcript_path);  // Claude Code only: others have no transcript title
  const r = board.event(ev, Date.now());
  if (r.cleared && waiters.has(r.cleared)) waiters.get(r.cleared)(null);
  // A new prompt (typed at the keyboard or by voice) makes that session's older replies stale on the dial.
  if (ev.hook_event_name === 'UserPromptSubmit' && ev.session_id) sendLine({ evt: 'seen', sid: ev.session_id });
  if (r.turn) {
    const read = takeTalked(r.sid);  // a reply to a voice send: shown now, never queued to the bell
    sendLine({ evt: 'turn', role: 'assistant', content: [{ type: 'text', text: r.turn }], sid: r.sid,
               ...(read ? { read } : r.busy && { busy: r.busy }), ...r.speaker });
  }
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
                            sessions: sessionList(board).map((s) => ({ ...s, tty: board.tty(s.id) })),  // the voice assistant reads this
                            heartbeat: heartbeat(dials.find((d) => d.ready)) }, null, 2));
  }
  if (req.method !== 'POST') return res.writeHead(404).end();
  const ev = await body(req);
  if (req.url === '/speak') {  // {"text": "..."}: say it on the first ready dial; answers {"played": bool} after playback ends
    if (typeof ev?.text !== 'string') return res.writeHead(400).end();
    const played = await speak(null, ev.text).catch((e) => (log('speak failed:', e.message), false));
    return res.writeHead(200, { 'content-type': 'application/json' }).end(JSON.stringify({ played }));
  }
  if (req.url === '/permission') return onPermission(ev, res);
  if (req.url === '/event') {
    if (ev) onEvent(ev);
    return res.writeHead(204).end();
  }
  res.writeHead(404).end();
});
server.listen(PORT, '127.0.0.1', () => log(`listening on 127.0.0.1:${PORT}, dials ${HOSTS.join(', ')}`));

dials.forEach(connectDial);

// ---- Grok Bot store (read-only) ----
let grokBots = [];  // botLooks of every roster: the idle face cycles Clawd and these
// The app rewrites whole blob files; fs.watch fires several times per write, so debounce per file.
function watchGrokBot(dir) {
  if (!existsSync(dir)) return log(`grok bot store not found, skipping: ${dir}`);
  const names = new Map();   // conversation uuid -> { name, avatar, color }, merged over every account's roster
  const rosters = new Map(); // file -> Map
  const seqs = new Map();    // transcript file -> last seq seen
  const timers = new Map();
  const loadRoster = (f) => {
    rosters.set(f, readRoster(join(dir, f)));
    names.clear();
    for (const r of rosters.values()) for (const [k, v] of r) names.set(k, v);
    grokBots = botLooks(names);
    push();
  };
  const kind = (f) => {
    const key = decodeKey(f);
    return key.endsWith('.roster.last-roster') ? 'roster' : conversationId(key) ? 'transcript' : '';
  };
  const files = readdirSync(dir);
  for (const f of files) if (kind(f) === 'roster') loadRoster(f);
  for (const f of files) if (kind(f) === 'transcript') seqs.set(f, eventsSince(join(dir, f), -1).lastSeq); // baseline, nothing emitted
  const handle = (f) => {
    const k = kind(f);
    if (k === 'roster') return loadRoster(f);
    if (k !== 'transcript') return;
    const { events, lastSeq } = eventsSince(join(dir, f), seqs.get(f) ?? -1, names);
    seqs.set(f, lastSeq);
    if (!events.length) return;
    log(`grok ${names.get(conversationId(decodeKey(f)))?.name || 'Grok Bot'}: ${events.length} events`);
    events.forEach(onEvent);
  };
  watch(dir, (_type, f) => {
    if (!f) return;
    clearTimeout(timers.get(f));
    timers.set(f, setTimeout(() => { timers.delete(f); handle(f); }, 300));
  }).on('error', (e) => log('grok watch error:', e.message));
  log(`grok bot store watched: ${dir} (${seqs.size} conversations)`);
}
watchGrokBot(GROK_STORE);
