// Grok Bot store blobs -> Claude-shaped hook events for board.mjs. Pure: reads a given path, no watcher.
// Store: ~/Library/Application Support/Grok Bot/sand-client-persistence/<base32 key>.blob (JSON).

import { readFileSync } from 'node:fs';

const TRANSCRIPT = '.transcript.replicas.';

// File name (base32 of the key, no padding) -> key. '' if it is not a blob name.
export function decodeKey(fileName) {
  if (!fileName.endsWith('.blob')) return '';
  let s = fileName.slice(0, -5).toUpperCase();
  if (!/^[A-Z2-7]*$/.test(s)) return '';
  s += '='.repeat((8 - (s.length % 8)) % 8);
  try { return base32(s); } catch { return ''; }
}

function base32(s) {
  const A = 'ABCDEFGHIJKLMNOPQRSTUVWXYZ234567';
  let bits = 0, val = 0;
  const out = [];
  for (const c of s.replace(/=+$/, '')) {
    val = (val << 5) | A.indexOf(c);
    bits += 5;
    if (bits >= 8) { bits -= 8; out.push((val >> bits) & 0xff); }
  }
  return Buffer.from(out).toString('utf8');
}

const readJson = (path) => JSON.parse(readFileSync(path, 'utf8'));

// The app's avatar picker: roster shape name -> outline name (its o_), colour id -> swatch. Black draws white on the
// app's dark theme, and on the dial. Empty shape/colour is the app's default, blob/black.
const SHAPE_ALIAS = { blob: 'sphere', flower: 'starFlower', heart: 'heartChubby', star: 'star6' };
const SWATCH = { black: 0xFFFFFF, brown: 0x936439, red: 0xFF263C, orange: 0xFF6700, yellow: 0xFF9800, green: 0x00C972,
                 cyan: 0x00BCA6, blue: 0x1084FE, violet: 0x9159FE, magenta: 0xFF309B, gray: 0x777777 };

// roster blob -> Map<conversation uuid, { name, avatar, color, group }>; avatar is an art/grok shape, color 0xRRGGBB.
// Group chats are rows too (isGroup, memberIds): they keep their name but have no bot look of their own.
// Empty map on a bad file.
export function readRoster(path) {
  const m = new Map();
  try {
    for (const r of readJson(path).value.rows) {
      if (!r?.id || !r.name) continue;
      const shape = r.avatarShape || 'blob';
      const group = !!r.isGroup, gone = !!(r.isHiddenFromSidebar || r.isRemovedFromSidebar);
      m.set(r.id, group ? { name: r.name, group }
        : { name: r.name, avatar: SHAPE_ALIAS[shape] ?? shape, color: SWATCH[r.avatarColor] ?? SWATCH.black, ...(gone && { gone }) });
    }
  } catch { /* partial write or wrong shape */ }
  return m;
}

// Every distinct bot in a readRoster map, roster order: [[avatar, color, name], ...]. The dial's idle face cycles them.
// Not group chats, nor bots hidden or removed from the app's sidebar.
export function botLooks(names) {
  const seen = new Map();
  for (const b of names.values()) if (!b.group && !b.gone) seen.set(`${b.avatar}/${b.color}/${b.name}`, [b.avatar, b.color, b.name]);
  return [...seen.values()];
}

export function conversationId(key) {
  const i = key.indexOf(TRANSCRIPT);
  return i < 0 ? null : key.slice(i + TRANSCRIPT.length) || null;
}

function toEvent(e) {
  const text = (s) => typeof s === 'string' && s.trim() !== '';
  if (e.kind === 'message') {
    if (e.role === 'user') return { hook_event_name: 'UserPromptSubmit' };
    if (e.role === 'assistant' && e.isStreaming === false && text(e.content))
      return { hook_event_name: 'Stop', last_assistant_message: e.content };
  } else if (e.kind === 'send-message') {
    const m = e.message;
    if (m?.type === 'text' && text(m.content)) return { hook_event_name: 'Stop', last_assistant_message: m.content };
    if (m?.type === 'local-tool-permission' && m.ask) {
      const a = m.ask, call = { tool_name: a.action, tool_input: { command: a.target } };
      if (a.status === 'pending') return { hook_event_name: 'GrokPermissionPending', ...call, request_id: a.requestId };
      if (a.status === 'allowed' || a.status === 'always') return { hook_event_name: 'PreToolUse', ...call };
      if (['expired', 'denied', 'never'].includes(a.status))
        return { hook_event_name: 'GrokPermissionResolved', request_id: a.requestId };
    }
  }
  return null;
}

// Events for the transcript blob at `path` with seq > afterSeq, oldest first.
// names: Map from readRoster. Never throws; a bad/partial file gives no events.
export function eventsSince(path, afterSeq, names = new Map()) {
  const none = { events: [], lastSeq: afterSeq };
  let entries, uuid;
  try {
    entries = readJson(path).value.entries;
    uuid = conversationId(decodeKey(path.split('/').pop()));
    if (!Array.isArray(entries) || !uuid) return none;
  } catch { return none; }
  const seqOf = (e) => (typeof e.seq === 'number' ? e.seq : -Infinity);
  const fresh = entries.filter((e) => e && seqOf(e) > afterSeq)
    .sort((a, b) => seqOf(a) - seqOf(b) || (a.timestampMs ?? 0) - (b.timestampMs ?? 0));
  const bot = names.get(uuid);
  const base = { session_id: 'grok-bot:' + uuid, cwd: '/grok/' + (bot?.name || 'Grok Bot'),
                 ...(!bot?.group && { avatar: bot?.avatar ?? 'sphere', avatar_color: bot?.color ?? SWATCH.black }) };
  const events = [];
  for (const e of fresh) {
    const ev = toEvent(e);
    if (ev) events.push({ ...ev, ...base });
  }
  return { events, lastSeq: fresh.length ? seqOf(fresh.at(-1)) : afterSeq };
}
