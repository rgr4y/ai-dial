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
