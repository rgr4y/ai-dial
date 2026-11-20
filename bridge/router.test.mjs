import assert from 'node:assert/strict';
import { test } from 'node:test';
import { Board } from './board.mjs';
import { parseJson, route, sessionList } from './router.mjs';

const mk = (prompts) => {
  const board = new Board(), now = 1e12;
  board.event({ session_id: 'a1', hook_event_name: 'UserPromptSubmit', cwd: '/x/api' }, now);
  board.event({ session_id: 'd1', hook_event_name: 'Stop', cwd: '/x/auto-ai' }, now);
  for (const [sid, cwd] of prompts) board.openPrompt({ session_id: sid, tool_name: 'Bash', tool_input: { command: 'rm x' }, cwd }, now);
  return sessionList(board, now);
};
const one = mk([['a1', '/x/api']]), two = mk([['a1', '/x/api'], ['d1', '/x/auto-ai']]);
const env = { apiKey: 'k', baseUrl: 'http://proxy' };

// Fake fetch: answers with a thinking block then the given text block; records the request.
const fake = (text, { ok = true, status = 200 } = {}) => {
  const f = async (url, init) => { f.calls.push([url, init]); return { ok, status, json: async () => ({ content: [{ type: 'thinking', thinking: '...' }, { type: 'text', text }] }) }; };
  f.calls = [];
  return f;
};
const go = (text, fetchFn, o = {}) => route({ text, sessions: o.sessions || one, focusedId: 'focusedId' in o ? o.focusedId : 'd1', env, fetchFn });
const never = async () => { throw new Error('model must not be called'); };

test('session list carries name, status, open prompt', () => {
  assert.deepEqual(one.map((s) => [s.id, s.name, s.status, !!s.prompt]), [['a1', 'api', 'waiting', true], ['d1', 'auto-ai', 'idle', false]]);
});
test('rule: tell <name> to X (spoken name matches hyphenated session)', async () => {
  assert.deepEqual(await go('Tell auto ai to run the tests', never), { action: 'send', sid: 'd1', text: 'run the tests', via: 'rule' });
});
test('rule: "<name>, X" and leading name', async () => {
  assert.deepEqual(await go('API, fix the lint', never, { focusedId: 'd1' }), { action: 'send', sid: 'a1', text: 'fix the lint', via: 'rule' });
  assert.deepEqual(await go('api fix the lint', never), { action: 'send', sid: 'a1', text: 'fix the lint', via: 'rule' });
});
test('rule: a name prefix of another word is not a name', async () => {
  const f = fake('{"target":"focused","text":"apiary notes"}');
  assert.equal((await go('apiary notes', f)).via, 'model');
});
test('rule: allow with exactly one open prompt answers it', async () => {
  assert.deepEqual(await go('Allow.', never), { action: 'answer', sid: 'a1', decision: 'allow', via: 'rule' });
  assert.deepEqual(await go('no', never), { action: 'answer', sid: 'a1', decision: 'deny', via: 'rule' });
});
test('allow with two open prompts goes to the model', async () => {
  const f = fake('{"target":"assistant","text":"which one?"}');
  const r = await go('allow', f, { sessions: two });
  assert.equal(r.via, 'model');
  assert.equal(f.calls.length, 1);
});
test('model JSON: request shape, text block read past the thinking block', async () => {
  const f = fake('```json\n{"target":"focused","text":"fix the lint errors"}\n```');
  const r = await go('fix the lint errors', f);
  assert.deepEqual(r, { action: 'send', sid: 'd1', text: 'fix the lint errors', via: 'model' });
  const [url, init] = f.calls[0], body = JSON.parse(init.body);
  assert.equal(url, 'http://proxy/v1/messages');
  assert.equal(init.headers['x-api-key'], 'k');
  assert.ok(body.max_tokens >= 1000 && !body.tool_choice && !body.tools);
  assert.match(body.messages[0].content, /"focused":true/);
});
test('model JSON: named session, assistant, unknown name', async () => {
  assert.deepEqual(await go('please have api rerun', fake('{"target":"API","text":"rerun"}')), { action: 'send', sid: 'a1', text: 'rerun', via: 'model' });
  assert.deepEqual(await go('open a new terminal', fake('{"target":"assistant","text":"open a new terminal"}')), { action: 'assistant', text: 'open a new terminal', via: 'model' });
  assert.equal((await go('x y z', fake('{"target":"ghost","text":"hi"}'))).action, 'speak');
});
test('model: focused with nothing on the face -> sid undefined (bridge asks Which session?)', async () => {
  const r = await go('hello there', fake('{"target":"focused","text":"hello there"}'), { focusedId: undefined });
  assert.deepEqual([r.action, r.sid], ['send', undefined]);
});
test('model names a session and says allow while it has a prompt -> answer', async () => {
  assert.equal((await go('say yes to api', fake('{"target":"api","text":"yes"}'), { sessions: two })).action, 'answer');
});
test('timeout / HTTP error / junk -> focused, or assistant with no focus; reason logged', async () => {
  const slow = (url, { signal }) => new Promise((_, rej) => signal.addEventListener('abort', () => rej(new Error('timed out'))));
  const r = await route({ text: 'hello', sessions: one, focusedId: 'd1', env, fetchFn: slow, timeoutMs: 20 });
  assert.deepEqual([r.action, r.sid, r.via], ['send', 'd1', 'fallback']);
  assert.match(r.why, /api: /);
  assert.match((await go('hello', fake('', { ok: false, status: 500 }))).why, /HTTP 500/);
  assert.equal((await go('hello', fake('no json here'))).why, 'no usable JSON');
  const a = await go('hello', fake('no json here'), { focusedId: undefined });
  assert.deepEqual([a.action, a.text, a.via], ['assistant', 'hello', 'fallback']);
});
test('no key: fallback with reason', async () => {
  const r = await route({ text: 'hello', sessions: one, focusedId: 'd1', env: {}, fetchFn: never });
  assert.deepEqual([r.action, r.why], ['send', 'no ANTHROPIC_API_KEY']);
});
test('parseJson', () => {
  assert.deepEqual(parseJson('ok {"target":"a","text":"b"} done'), { target: 'a', text: 'b' });
  assert.equal(parseJson('{"target":"a"}'), null);
  assert.equal(parseJson('{bad'), null);
});
