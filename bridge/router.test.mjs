import assert from 'node:assert/strict';
import { test } from 'node:test';
import { Board } from './board.mjs';
import { route, sessionList } from './router.mjs';

const board = new Board();
const now = 1e12;
board.event({ session_id: 'a1', hook_event_name: 'UserPromptSubmit', cwd: '/x/api' }, now);
board.event({ session_id: 'd1', hook_event_name: 'Stop', cwd: '/x/dashboard' }, now);
board.openPrompt({ session_id: 'a1', tool_name: 'Bash', tool_input: { command: 'rm x' }, cwd: '/x/api' }, now);
const sessions = sessionList(board, now);

// Fake model: answers with the given tool call; records what it was asked.
const fake = (name, input, extra = {}) => {
  const c = { calls: [], messages: { create: async (p, o) => { c.calls.push([p, o]); return { stop_reason: 'tool_use', content: [{ type: 'tool_use', name, input }], ...extra }; } } };
  return c;
};
const go = (text, client, focusedId = 'd1') => route({ text, sessions, focusedId, client });

test('session list carries name, status, open prompt', () => {
  assert.deepEqual(sessions.map((s) => [s.id, s.name, s.status, !!s.prompt]), [['a1', 'api', 'waiting', true], ['d1', 'dashboard', 'idle', false]]);
});
test('named session', async () => {
  const c = fake('send_to_session', { session: 'API', text: 'run the tests' });
  assert.deepEqual(await go('tell api to run the tests', c), { action: 'send', sid: 'a1', text: 'run the tests', via: 'model' });
  assert.equal(c.calls[0][1].timeout, 3000);
  assert.match(c.calls[0][0].messages[0].content, /"focused":true/);
});
test('plain dictation -> focused', async () => {
  const r = await go('fix the lint errors', fake('send_to_session', { text: 'fix the lint errors' }));
  assert.deepEqual(r, { action: 'send', sid: 'd1', text: 'fix the lint errors', via: 'model' });
});
test('prompt answer', async () => {
  assert.deepEqual(await go('allow', fake('answer_prompt', { decision: 'allow' })), { action: 'answer', sid: 'a1', decision: 'allow', via: 'model' });
});
test('answer with no open prompt asks back', async () => {
  const r = await go('yes', fake('answer_prompt', { decision: 'allow', session: 'dashboard' }));
  assert.equal(r.action, 'speak');
});
test('status question -> speak', async () => {
  const r = await go('what is waiting', fake('speak', { text: 'api waits on Bash' }));
  assert.deepEqual(r, { action: 'speak', text: 'api waits on Bash', via: 'model' });
});
test('model names a session not in the list: ask back, never send', async () => {
  const r = await go('tell ghost hi', fake('send_to_session', { session: 'ghost', text: 'hi' }));
  assert.equal(r.action, 'speak');
});
test('no client (no API key): focused, logged reason', async () => {
  assert.deepEqual(await go('hello', null), { action: 'send', sid: 'd1', text: 'hello', via: 'fallback', why: 'no ANTHROPIC_API_KEY' });
});
test('api error / refusal / junk -> focused fallback', async () => {
  const boom = { messages: { create: async () => { throw new Error('timeout'); } } };
  assert.match((await go('hello', boom)).why, /api: timeout/);
  assert.equal((await go('hello', fake('send_to_session', { text: 'x' }, { stop_reason: 'refusal' }))).why, 'model refused');
  const r = await go('hello', fake('nope', {}));
  assert.deepEqual([r.action, r.sid, r.text, r.via], ['send', 'd1', 'hello', 'fallback']);
});
