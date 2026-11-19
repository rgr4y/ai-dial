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

// roster blob -> Map<conversation uuid, name>. Empty map on a bad file.
export function readRoster(path) {
  const m = new Map();
  try {
    for (const r of readJson(path).value.rows) if (r?.id && r.name) m.set(r.id, r.name);
  } catch { /* partial write or wrong shape */ }
  return m;
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
    if (m?.type === 'local-tool-permission' && m.ask)
      return { hook_event_name: 'PreToolUse', tool_name: m.ask.action, tool_input: { command: m.ask.target } };
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
  const base = { session_id: 'grok-bot:' + uuid, cwd: '/grok/' + (names.get(uuid) || 'Grok Bot') };
  const events = [];
  for (const e of fresh) {
    const ev = toEvent(e);
    if (ev) events.push({ ...ev, ...base });
  }
  return { events, lastSeq: fresh.length ? seqOf(fresh.at(-1)) : afterSeq };
}
