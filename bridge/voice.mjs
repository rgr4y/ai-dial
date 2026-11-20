// Voice for the dial: a minimal Wyoming STT client, the ESPHome voice-assistant messages the API library lacks, and
// typing the result into an iTerm session. Pure helpers + small I/O; tested by voice.test.mjs.
//
// Wyoming framing (OHF-Voice/wyoming; the Mac's servers speak it on :10300): a JSON header line
// {"type","version","data_length","payload_length"}\n, then data_length bytes of JSON (merged into event data), then
// payload_length raw bytes. Client sends transcribe, audio-start, audio-chunk*, audio-stop; the server answers with
// transcript-chunk* (partials; only the Swift AppleSTT server sends them) and one transcript.

import { execFile } from 'node:child_process';
import { createRequire } from 'node:module';
import { randomBytes } from 'node:crypto';
import { existsSync, readFileSync } from 'node:fs';
import { homedir } from 'node:os';
import net from 'node:net';

export function encode(type, data, payload) {
  const d = data && Object.keys(data).length ? Buffer.from(JSON.stringify(data)) : null;
  const header = { type, version: '1.0', ...(d && { data_length: d.length }), ...(payload && { payload_length: payload.length }) };
  return Buffer.concat([Buffer.from(JSON.stringify(header) + '\n'), d ?? Buffer.alloc(0), payload ?? Buffer.alloc(0)]);
}

// Feed it socket bytes; push() returns the complete events so far ({ type, data, payload }).
export class Decoder {
  buf = Buffer.alloc(0);
  push(chunk) {
    this.buf = Buffer.concat([this.buf, chunk]);
    const out = [];
    for (;;) {
      const nl = this.buf.indexOf(10);
      if (nl < 0) break;
      const h = JSON.parse(this.buf.subarray(0, nl).toString());
      const dl = h.data_length || 0, pl = h.payload_length || 0;
      if (this.buf.length < nl + 1 + dl + pl) break;
      const data = { ...h.data, ...(dl && JSON.parse(this.buf.subarray(nl + 1, nl + 1 + dl).toString())) };
      out.push({ type: h.type, data, payload: pl ? this.buf.subarray(nl + 1 + dl, nl + 1 + dl + pl) : null });
      this.buf = this.buf.subarray(nl + 1 + dl + pl);
    }
    return out;
  }
}

// One utterance against a Wyoming STT server. audio(pcm16) per chunk, finish() sends audio-stop and resolves with the
// final text (rejects on error, close without a transcript, or timeoutMs). onPartial(text) for each transcript-chunk.
export function openStt({ host = '127.0.0.1', port = 10300, rate = 16000, onPartial, timeoutMs = 8000 }) {
  const sock = net.connect(port, host), dec = new Decoder(), fmt = { rate, width: 2, channels: 1 };
  let text = null, err = null, waiter = null, timer = null;
  const settle = () => {
    if (!waiter || (text === null && !err)) return;
    clearTimeout(timer);
    const w = waiter;
    waiter = null;
    sock.destroy();
    err ? w.rej(err) : w.res(text);
  };
  sock.on('data', (d) => {
    try {
      for (const e of dec.push(d)) {
        if (e.type === 'transcript-chunk') onPartial?.(e.data.text || '');
        else if (e.type === 'transcript') text = e.data.text || '';
        else if (e.type === 'error') err = new Error(e.data.text || 'stt error');
      }
    } catch (x) {
      err = x;
    }
    settle();
  });
  sock.on('error', (e) => { err ??= e; settle(); });
  sock.on('close', () => { if (text === null) err ??= new Error('stt closed'); settle(); });
  sock.write(encode('transcribe', {}));
  sock.write(encode('audio-start', fmt));
  return {
    audio: (pcm) => { if (!sock.destroyed) sock.write(encode('audio-chunk', fmt, pcm)); },
    finish() {
      if (!sock.destroyed) sock.write(encode('audio-stop'));
      return new Promise((res, rej) => {
        waiter = { res, rej };
        timer = setTimeout(() => { err ??= new Error('stt timeout'); settle(); }, timeoutMs);
        settle();
      });
    },
    abort: () => sock.destroy(),
  };
}

// ---- speaking: claude-speak text rules -> Wyoming TTS -> WAV ----
// Rules: ~/workspace/claude-speak's speak-rules.json (the ~/.config/claude-speak copy wins, as in claude-speak.py),
// applied the way its filter_text() does: in file order, one pass each. Rules are Python regexes; the few Python-only
// spellings are translated ("(?i)" prefix, \1 in replacements). A rule that won't compile is skipped, like there.
// Only the rules engine is reused: no claude-speak servers, no sentence split (the TTS server takes the whole text).
const RULES_FILES = [`${homedir()}/.config/claude-speak/speak-rules.json`, `${homedir()}/workspace/claude-speak/speak-rules.json`];
let rulesCache = null;
export function loadRules(file = RULES_FILES.find(existsSync)) {
  if (!file) return [];
  try {
    return JSON.parse(readFileSync(file, 'utf8')).filter((r) => r.type);
  } catch {
    return [];
  }
}
const re = (pat, flags) => {
  const i = /^\(\?i\)/.test(pat);
  return new RegExp(pat.replace(/^\(\?i\)/, '').replace(/\\b\?/g, ''), flags + (i ? 'i' : ''));
};
const mungePath = (p) => p.replace(/^[~/]+/, '').replaceAll('.', ' dot ').replace(/[/_-]/g, ' ').trim();
export function applySpeakRules(text, rules = (rulesCache ??= loadRules())) {
  for (const r of rules) {
    if (r.enabled === false) continue;
    try {
      if (r.type === 'strip_re') text = text.replace(re(r.pattern, 'gi'), '');
      else if (r.type === 'munge_path') text = text.replace(re(r.pattern, 'g'), mungePath);
      else if (r.type === 'replace') text = text.replaceAll(r.find, r.replacement);
      else if (r.type === 'replace_re') text = text.replace(re(r.pattern, r.ignore_case ? 'gi' : 'g'), (...m) => r.replacement.replace(/\\(\d)/g, (_, n) => (typeof m[n] === 'string' ? m[n] : '')));
      else if (r.type === 'spell_out') {
        const spoken = r.spoken || [...r.word].filter((c) => /[\p{L}\p{N}]/u.test(c)).join(' ').toUpperCase();
        text = text.replace(new RegExp(`\\b${r.word.replace(/[.*+?^${}()|[\]\\]/g, '\\$&')}\\b`, 'gi'), () => spoken);
      }
    } catch {}
  }
  return text.replace(/ {2,}/g, ' ').trim();
}

// 16-bit little-endian PCM -> mono 16 kHz (linear interpolation; the Wyoming say server already sends 16 kHz mono,
// so this is the safety net the bead asks for).
function to16kMono(pcm, rate, channels) {
  const n = Math.floor(pcm.length / 2 / channels);
  const mono = new Float64Array(n);
  for (let i = 0; i < n; i++) {
    let sum = 0;
    for (let c = 0; c < channels; c++) sum += pcm.readInt16LE((i * channels + c) * 2);
    mono[i] = sum / channels;
  }
  if (rate === 16000) return Buffer.from(Int16Array.from(mono, Math.round).buffer);
  const out = new Int16Array(Math.floor((n * 16000) / rate));
  for (let i = 0; i < out.length; i++) {
    const x = (i * rate) / 16000, j = Math.floor(x), f = x - j;
    out[i] = Math.round(mono[j] * (1 - f) + (mono[Math.min(j + 1, n - 1)] ?? 0) * f);
  }
  return Buffer.from(out.buffer);
}

export function wav16k(pcm) {
  const h = Buffer.alloc(44);
  h.write('RIFF', 0); h.writeUInt32LE(36 + pcm.length, 4); h.write('WAVEfmt ', 8);
  h.writeUInt32LE(16, 16); h.writeUInt16LE(1, 20); h.writeUInt16LE(1, 22);
  h.writeUInt32LE(16000, 24); h.writeUInt32LE(32000, 28); h.writeUInt16LE(2, 32); h.writeUInt16LE(16, 34);
  h.write('data', 36); h.writeUInt32LE(pcm.length, 40);
  return Buffer.concat([h, pcm]);
}

// Wyoming synthesize: sends {text}, collects audio-start / audio-chunk* / audio-stop, resolves a 16 kHz mono 16-bit WAV
// Buffer (null when the text has nothing speakable after the rules). Rejects on error, close without audio, timeout,
// or a sample width other than 16 bit. wyoming-say.py always answers 16000 Hz / 2 bytes / 1 channel.
export function synthesize(text, { host = '127.0.0.1', port = 10200, timeoutMs = 15000, rules } = {}) {
  const spoken = applySpeakRules(cleanText(text), rules);
  if (!/[\p{L}\p{N}]/u.test(spoken)) return Promise.resolve(null);  // the server stays silent on empty text: don't wait for it
  return new Promise((res, rej) => {
    const sock = net.connect(port, host), dec = new Decoder(), chunks = [];
    let fmt = null, done = false;
    const finish = (err, val) => {
      if (done) return;
      done = true;
      clearTimeout(timer);
      sock.destroy();
      err ? rej(err) : res(val);
    };
    const timer = setTimeout(() => finish(new Error('tts timeout')), timeoutMs);
    sock.on('data', (d) => {
      try {
        for (const e of dec.push(d)) {
          if (e.type === 'audio-start') fmt = e.data;
          else if (e.type === 'audio-chunk') { fmt ??= e.data; if (e.payload) chunks.push(e.payload); }
          else if (e.type === 'error') return finish(new Error(e.data.text || 'tts error'));
          else if (e.type === 'audio-stop') {
            if (!chunks.length) return finish(new Error('tts returned no audio'));
            if (fmt.width !== 2) return finish(new Error(`tts sample width ${fmt.width}`));
            return finish(null, wav16k(to16kMono(Buffer.concat(chunks), fmt.rate, fmt.channels || 1)));
          }
        }
      } catch (x) {
        finish(x);
      }
    });
    sock.on('error', finish);
    sock.on('close', () => finish(new Error('tts closed')));
    sock.write(encode('synthesize', { text: spoken }));
  });
}

// WAVs waiting for the dial to fetch them (served by the bridge's asset server as tts/<id>.wav); dropped after ttlMs.
export const ttsFiles = new Map();
export function stashTts(wav, ttlMs = 60000) {
  const id = randomBytes(8).toString('hex');
  ttsFiles.set(id, wav);
  setTimeout(() => ttsFiles.delete(id), ttlMs).unref();
  return id;
}
export const wavMs = (wav) => Math.round(((wav.length - 44) / 32000) * 1000);

// ---- ESPHome voice assistant over the native API ----
// @2colors/esphome-native-api 1.3.7 ships the protobuf classes but its id table stops short of the voice messages, so
// an incoming VoiceAssistantRequest (90) would be treated as a protocol error and drop the connection. The table is a
// plain exported object: add the ids at runtime (no patching of node_modules). Call once before connecting.
const VOICE_IDS = { 89: 'SubscribeVoiceAssistantRequest', 90: 'VoiceAssistantRequest', 91: 'VoiceAssistantResponse',
                    92: 'VoiceAssistantEventResponse', 106: 'VoiceAssistantAudio' };
export function registerVoiceMessages() {
  const { id_to_type, pb } = createRequire(import.meta.url)('@2colors/esphome-native-api/lib/utils/messages');
  for (const [id, t] of Object.entries(VOICE_IDS)) {
    id_to_type[id] = t;
    pb[t].type = t;
    pb[t].id = Number(id);
  }
  return pb;
}

// ---- typing into the session ----
// iTerm AppleScript: find the session whose tty matches, write the text without a newline, then a bare newline (Claude
// Code's input treats text + Return arriving in one burst as a paste, so Return goes separately). The text travels as
// an argv item, never inside the script.
const SCRIPT = [
  'on run argv',
  'if application "iTerm" is not running then return "none"',
  'set want to "/dev/" & item 1 of argv',
  'set msg to item 2 of argv',
  'tell application "iTerm"',
  'repeat with w in windows',
  'repeat with t in tabs of w',
  'repeat with s in sessions of t',
  'if (tty of s) is want then',
  'tell s to write text msg newline NO',
  'delay 0.3',
  'tell s to write text ""',
  'return "ok"',
  'end if',
  'end repeat',
  'end repeat',
  'end repeat',
  'end tell',
  'return "none"',
  'end run',
];

export const cleanText = (s) => String(s ?? '').replace(/[\x00-\x1f\x7f]+/g, ' ').replace(/\s+/g, ' ').trim();

// tmux: the session's tty is a pane, which iTerm never sees (it only knows the tmux client's tty). send-keys -l types
// the text literally; Enter goes separately, after the same 0.3 s as the iTerm path. The bridge's launchd PATH lacks
// Homebrew, hence the absolute paths.
const TMUX = ['/opt/homebrew/bin/tmux', '/usr/local/bin/tmux'].find((p) => existsSync(p));
const run = (cmd, args) => new Promise((res, rej) =>
  execFile(cmd, args, { timeout: 5000 }, (e, out) => (e ? rej(e) : res(out))));

async function tmuxPane(tty) {
  if (!TMUX) return null;
  const out = await run(TMUX, ['list-panes', '-a', '-F', '#{pane_tty} #{pane_id}']).catch(() => '');
  return out.split('\n').map((l) => l.split(' ')).find(([t]) => t === `/dev/${tty}`)?.[1] ?? null;
}

async function tmuxType(target, msg) {
  await run(TMUX, ['send-keys', '-t', target, '-l', msg]);
  await new Promise((r) => setTimeout(r, 300));
  await run(TMUX, ['send-keys', '-t', target, 'Enter']);
}

// A tmux session by name ("voice", the assistant): `=name:` matches exactly, never as a prefix, and means its active pane.
export async function tmuxHasSession(name) {
  return !!TMUX && run(TMUX, ['has-session', '-t', `=${name}`]).then(() => true, () => false);
}
// Resolves true when typed, false when the session is not running (or the text has no words).
export async function typeIntoTmuxSession(name, text) {
  const msg = cleanText(text);
  if (!/[\p{L}\p{N}]/u.test(msg) || !(await tmuxHasSession(name))) return false;
  await tmuxType(`=${name}:`, msg);
  return true;
}

// Resolves true when typed, false when no tmux pane or iTerm session has that tty. Rejects on tmux/osascript failure.
export async function typeIntoTty(tty, text) {
  const msg = cleanText(text);
  if (!/^ttys\d+$/.test(tty || '') || !/[\p{L}\p{N}]/u.test(msg)) return false;  // no words (STT heard only ".")
  const pane = await tmuxPane(tty);
  if (pane) {
    await tmuxType(pane, msg);
    return true;
  }
  return new Promise((res, rej) => {
    execFile('osascript', SCRIPT.flatMap((l) => ['-e', l]).concat([tty, msg]), { timeout: 10000 },
             (e, out) => (e ? rej(e) : res(out.trim() === 'ok')));
  });
}
