// Session state from agent hook events, folded into one Claude Hardware Buddy heartbeat.
// Pure: no I/O, time passed in. Tested by board.test.mjs.
//
// Input is Claude-Code-shaped hook JSON (hook_event_name, session_id, cwd, ...). Claude Code sends it
// directly; peon-ping's adapters translate Codex, Cursor, Gemini, ... into the same shape.
// Output is the Buddy heartbeat the dial already understands (REFERENCE.md "Heartbeat snapshot").

import { basename } from 'node:path';

export const STALE_MS = 30 * 60 * 1000;   // a working session silent this long is shown idle
export const FORGET_MS = 6 * 60 * 60 * 1000; // dropped entirely after this

const WORKING_EVENTS = new Set(['UserPromptSubmit', 'PreToolUse', 'PostToolUse', 'PostToolUseFailure',
                                'SubagentStart', 'SubagentStop', 'PreCompact', 'GrokPermissionResolved']);

// One-line summary of a tool call for the permission screen and the entries list.
export function summarize(tool, input) {
  if (!input || typeof input !== 'object') return tool || '';
  const v = input.command ?? input.file_path ?? input.path ?? input.url ?? input.pattern ?? input.query
    ?? input.description ?? input.prompt;
  if (typeof v === 'string') return (input.file_path || input.path) ? basename(v) : v.replace(/\s+/g, ' ').trim();
  const s = JSON.stringify(input);
  return s.length > 120 ? s.slice(0, 119) + '…' : s;
}

const hhmm = (ms) => new Date(ms).toTimeString().slice(0, 5);

export class Board {
  constructor() {
    this.sessions = new Map(); // session_id -> { project, status, at, tool, hint, prompt }
    this.entries = [];         // newest first, "HH:MM project: what"
    this.seq = 0;
  }

  _session(ev, now) {
    let s = this.sessions.get(ev.session_id);
    if (!s) {
      s = { project: basename(ev.cwd || '') || ev.session_id.slice(0, 8), status: 'idle', at: now, tool: '', hint: '',
            prompt: null, pending: null };
      this.sessions.set(ev.session_id, s);
    }
    if (ev.cwd) s.project = basename(ev.cwd);
    s.at = now;
    return s;
  }

  _entry(now, project, what) {
    this.entries.unshift(`${hhmm(now)} ${project}: ${what}`.slice(0, 80));
    this.entries.length = Math.min(this.entries.length, 5);
  }

  // Apply one hook event. Returns { cleared, turn } where cleared is the prompt id this event made moot
  // (answered in the terminal) and turn is assistant text to show on the done card.
  event(ev, now) {
    const out = { cleared: null, turn: null };
    if (!ev || !ev.session_id || !ev.hook_event_name) return out;
    const name = ev.hook_event_name;
    if (name === 'SessionEnd') {
      const s = this.sessions.get(ev.session_id);
      if (s?.prompt) out.cleared = s.prompt.id;
      this.sessions.delete(ev.session_id);
      return out;
    }
    const s = this._session(ev, now);
    // Any later event from a session with an open prompt means it was answered elsewhere.
    if (s.prompt && name !== 'PermissionRequest') {
      out.cleared = s.prompt.id;
      s.prompt = null;
    }
    // Display-only wait (Grok Bot): no answer path, so no prompt and no held hook. Any other event clears it.
    s.pending = null;
    if (name === 'GrokPermissionPending') {
      s.status = 'waiting';
      s.pending = { tool: ev.tool_name || 'tool', hint: summarize(ev.tool_name, ev.tool_input), at: now };
      this._entry(now, s.project, `asks ${s.pending.tool}`);
    } else if (WORKING_EVENTS.has(name)) {
      s.status = 'working';
      if (ev.tool_name) {
        s.tool = ev.tool_name;
        s.hint = summarize(ev.tool_name, ev.tool_input);
      }
      if (name === 'UserPromptSubmit') {
        s.tool = '';
        this._entry(now, s.project, 'prompt');
      } else if (name === 'PreToolUse') {
        this._entry(now, s.project, `${ev.tool_name} ${s.hint}`.trim());
      }
    } else if (name === 'Stop') {
      s.status = 'idle';
      s.tool = '';
      this._entry(now, s.project, 'done');
      const text = plainText(ev.last_assistant_message || '');
      if (text) out.turn = `${s.project}: ${text}`;
    } else if (name === 'SessionStart') {
      s.status = 'idle';
    }
    return out;
  }

  // A PermissionRequest that the daemon will hold open until the dial answers. Returns the prompt id.
  openPrompt(ev, now) {
    const s = this._session(ev, now);
    const id = `p${++this.seq}-${now.toString(36)}`;
    s.status = 'waiting';
    s.prompt = { id, tool: ev.tool_name || 'tool', hint: `${s.project}: ${summarize(ev.tool_name, ev.tool_input)}`, at: now };
    this._entry(now, s.project, `asks ${s.prompt.tool}`);
    return id;
  }

  // The dial answered, or the hook gave up: drop the prompt; the session keeps working.
  closePrompt(id) {
    for (const s of this.sessions.values()) {
      if (s.prompt?.id === id) {
        s.prompt = null;
        s.status = 'working';
        return true;
      }
    }
    return false;
  }

  prune(now) {
    for (const [k, s] of this.sessions) {
      if (now - s.at > FORGET_MS) this.sessions.delete(k);
      else if (s.status === 'working' && now - s.at > STALE_MS) s.status = 'idle';
    }
  }

  heartbeat(now) {
    this.prune(now);
    const all = [...this.sessions.values()];
    const working = all.filter((s) => s.status === 'working').sort((a, b) => b.at - a.at);
    const waiting = all.filter((s) => s.prompt).sort((a, b) => a.prompt.at - b.prompt.at);
    // Read-only waits (no allow/deny): count and first project, so the dial can show "waiting on the Mac".
    const pend = all.filter((s) => s.pending).sort((a, b) => a.pending.at - b.pending.at);
    const hb = { total: all.length, running: working.length, waiting: waiting.length, msg: '',
                 entries: this.entries, ...(pend.length && { watching: pend.length }) };
    if (waiting.length) {
      const p = waiting[0].prompt;
      hb.msg = `approve: ${p.tool}`;
      hb.prompt = { id: p.id, tool: p.tool, hint: p.hint };
    } else if (pend.length) {
      hb.msg = `${pend[0].project} waiting on the Mac`;
    } else if (working.length) {
      const w = working[0];
      hb.msg = w.tool ? `${w.project} ${w.tool}` : w.project;
    }
    return hb;
  }
}

// Assistant markdown -> one line of prose for the recap card (the dial draws plain text, ~90 characters).
// Drops code blocks and tables, then markdown markers; keeps the words in order.
export function plainText(md) {
  return md
    .replace(/```[\s\S]*?(```|$)/g, ' ')
    .split('\n')
    .filter((l) => !/^\s*\|/.test(l) && !/^\s*[-:| ]+$/.test(l))
    .map((l) => l.replace(/^\s*(#{1,6}\s+|>\s?|[-*+]\s+|\d+[.)]\s+)/, ''))
    .join(' ')
    .replace(/!?\[([^\]]*)\]\([^)]*\)/g, '$1')
    .replace(/(\*\*|__|\*|`)/g, '')
    .replace(/\s+/g, ' ')
    .trim();
}
