import assert from 'node:assert/strict';
import { test } from 'node:test';
import { Board, STALE_MS, plainText, summarize } from './board.mjs';

const ev = (name, extra = {}) => ({ hook_event_name: name, session_id: 's1', cwd: '/w/proj', ...extra });

test('a prompt makes the session working, Stop makes it idle with a turn', () => {
  const b = new Board();
  b.event(ev('UserPromptSubmit'), 1000);
  assert.equal(b.heartbeat(1000).running, 1);
  b.event(ev('PreToolUse', { tool_name: 'Bash', tool_input: { command: 'ls  -la' } }), 2000);
  assert.equal(b.heartbeat(2000).msg, 'proj Bash');
  const r = b.event(ev('Stop', { last_assistant_message: 'All done.' }), 3000);
  assert.equal(r.turn, 'proj: All done.');
  const hb = b.heartbeat(3000);
  assert.equal(hb.running, 0);
  assert.equal(hb.total, 1);
  assert.match(hb.entries[0], /proj: done$/);
});

test('permission prompt shows in the heartbeat and closes on answer or on a later event', () => {
  const b = new Board();
  const id = b.openPrompt(ev('PermissionRequest', { tool_name: 'Bash', tool_input: { command: 'rm -rf /tmp/x' } }), 1000);
  let hb = b.heartbeat(1000);
  assert.equal(hb.waiting, 1);
  assert.deepEqual(hb.prompt, { id, tool: 'Bash', hint: 'proj: rm -rf /tmp/x' });
  assert.ok(b.closePrompt(id));
  assert.equal(b.heartbeat(1000).prompt, undefined);

  const id2 = b.openPrompt(ev('PermissionRequest', { tool_name: 'Edit', tool_input: { file_path: '/a/b/c.ts' } }), 2000);
  assert.equal(b.heartbeat(2000).prompt.hint, 'proj: c.ts');
  // Answered in the terminal: the tool runs, PostToolUse arrives, the prompt is moot.
  assert.equal(b.event(ev('PostToolUse', { tool_name: 'Edit' }), 3000).cleared, id2);
  assert.equal(b.heartbeat(3000).waiting, 0);
});

test('oldest waiting prompt is shown; stale working sessions go idle', () => {
  const b = new Board();
  b.openPrompt({ ...ev('PermissionRequest', { tool_name: 'A' }), session_id: 'a' }, 1000);
  b.openPrompt({ ...ev('PermissionRequest', { tool_name: 'B' }), session_id: 'b' }, 2000);
  assert.equal(b.heartbeat(2000).prompt.tool, 'A');
  b.event({ ...ev('UserPromptSubmit'), session_id: 'c' }, 3000);
  assert.equal(b.heartbeat(3000 + STALE_MS + 1).running, 0);
});

test('a session made only of a prompt is dropped when the prompt closes; a real one keeps working', () => {
  const b = new Board();
  const id = b.openPrompt({ ...ev('PermissionRequest', { tool_name: 'Bash' }), session_id: 'curl' }, 1000);
  assert.ok(b.closePrompt(id));
  assert.equal(b.heartbeat(1000).total, 0);

  b.event(ev('PreToolUse', { tool_name: 'Bash' }), 2000);
  const id2 = b.openPrompt(ev('PermissionRequest', { tool_name: 'Bash' }), 2000);
  assert.ok(b.closePrompt(id2));
  const hb = b.heartbeat(2000);
  assert.equal(hb.total, 1);
  assert.equal(hb.running, 1);
});

test('summarize', () => {
  assert.equal(summarize('Bash', { command: 'git\n status' }), 'git status');
  assert.equal(summarize('Read', { file_path: '/x/y.md' }), 'y.md');
  assert.equal(summarize('X', null), 'X');
});

test('plainText strips markdown, tables and code', () => {
  const md = 'Fast connect **works** now. Times:\n\n| Step | Now |\n|---|---|\n| Wi-Fi | 5.1 s |\n\n- **Wi-Fi:** see `fast_connect`\n```js\nx()\n```\n[docs](http://x)';
  assert.equal(plainText(md), 'Fast connect works now. Times: Wi-Fi: see fast_connect docs');
  assert.equal(plainText('| a |\n|---|'), '');
});

test('Grok pending ask is display-only: waiting, no prompt, cleared by resolve or any later event', () => {
  const b = new Board();
  b.event(ev('GrokPermissionPending', { tool_name: 'run-command', tool_input: { command: 'echo hi' }, request_id: 'ab'.repeat(32) }), 1000);
  let hb = b.heartbeat(1000);
  assert.equal(b.sessions.get('s1').status, 'waiting');
  assert.deepEqual([hb.waiting, hb.running, hb.watching, hb.prompt], [0, 0, 1, undefined]);
  assert.equal(hb.msg, 'proj waiting on the Mac');
  b.event(ev('GrokPermissionResolved', { request_id: 'ab'.repeat(32) }), 2000);
  hb = b.heartbeat(2000);
  assert.deepEqual([b.sessions.get('s1').status, hb.watching, hb.running], ['working', undefined, 1]);
  b.event(ev('GrokPermissionPending', { tool_name: 'run-command' }), 3000);
  b.event(ev('Stop'), 4000);
  assert.deepEqual([b.sessions.get('s1').status, b.heartbeat(4000).watching], ['idle', undefined]);
});

test('a Grok Bot session busy shows its avatar; a Claude session does not', () => {
  const b = new Board();
  b.event(ev('UserPromptSubmit', { avatar: 'cloud', avatar_color: 0x00BCA6 }), 1000);
  b.event(ev('PreToolUse', { tool_name: 'run-command' }), 1500);  // later events carry it too; the session keeps it
  let hb = b.heartbeat(1500);
  assert.deepEqual([hb.avatar, hb.avatar_color, hb.avatar_name], ['cloud', 0x00BCA6, 'proj']);
  b.event(ev('UserPromptSubmit', { session_id: 's2', cwd: '/w/other' }), 2000);
  hb = b.heartbeat(2000);
  assert.equal(hb.msg, 'other');
  assert.equal(hb.avatar, undefined);
  b.event(ev('Stop', { session_id: 's2' }), 3000);
  b.event(ev('Stop'), 3000);
  assert.equal(b.heartbeat(3000).avatar, undefined);
});

test('a busy Codex session sends its agent', () => {
  const b = new Board();
  b.event(ev('UserPromptSubmit', { agent: 'codex' }), 1000);
  b.event(ev('PreToolUse', { agent: 'codex', tool_name: 'Bash' }), 1500);
  assert.equal(b.heartbeat(1500).agent, 'codex');
  b.event(ev('Stop', { agent: 'codex' }), 2000);
  assert.equal(b.heartbeat(2000).agent, undefined);
});

test('Copilot -p: prompt before a new SessionStart stays working; a resumed start goes idle', () => {
  const b = new Board();
  b.event(ev('UserPromptSubmit', { agent: 'copilot' }), 1000);
  b.event(ev('SessionStart', { agent: 'copilot', source: 'new' }), 1100);
  assert.equal(b.heartbeat(1100).agent, 'copilot');
  b.event(ev('SessionStart', { agent: 'copilot', source: 'resume' }), 1200);
  assert.equal(b.heartbeat(1200).running, 0);
});

test('a reply names its speaker: a Grok bot by its look, Codex by agent, Claude Code by nothing', () => {
  const b = new Board();
  b.event(ev('UserPromptSubmit', { avatar: 'cloud', avatar_color: 0x00BCA6 }), 1000);
  let r = b.event(ev('Stop', { last_assistant_message: 'Done.' }), 2000);
  assert.deepEqual(r.speaker, { avatar: 'cloud', avatar_color: 0x00BCA6, avatar_name: 'proj' });
  b.event(ev('UserPromptSubmit', { session_id: 's2', agent: 'codex' }), 3000);
  r = b.event(ev('Stop', { session_id: 's2', last_assistant_message: 'Ok.' }), 4000);
  assert.deepEqual(r.speaker, { agent: 'codex' });
  b.event(ev('UserPromptSubmit', { session_id: 's3', cwd: '/w/cc' }), 5000);
  r = b.event(ev('Stop', { session_id: 's3', cwd: '/w/cc', last_assistant_message: 'Hi.' }), 6000);
  assert.equal(r.turn, 'cc: Hi.');
  assert.equal(r.speaker, null);
});

test('a session name (Claude Code /rename) replaces the dirname', () => {
  const b = new Board();
  b.event(ev('UserPromptSubmit', { cwd: '/w/autonomous-ai', session_name: 'auto-ai' }), 1000);
  assert.equal(b.heartbeat(1000).msg, 'auto-ai');
  b.event(ev('PreToolUse', { cwd: '/w/autonomous-ai', tool_name: 'Bash' }), 1100);  // no name on this event: keeps it
  assert.equal(b.heartbeat(1100).msg, 'auto-ai Bash');
});
