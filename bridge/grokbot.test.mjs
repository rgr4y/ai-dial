import test from 'node:test';
import assert from 'node:assert/strict';
import { mkdtempSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { decodeKey, readRoster, conversationId, eventsSince } from './grokbot.mjs';

const b32 = (s) => {
  const A = 'abcdefghijklmnopqrstuvwxyz234567';
  let bits = '';
  for (const b of Buffer.from(s)) bits += b.toString(2).padStart(8, '0');
  let out = '';
  for (let i = 0; i < bits.length; i += 5) out += A[parseInt(bits.slice(i, i + 5).padEnd(5, '0'), 2)];
  return out + '.blob';
};
const dir = mkdtempSync(join(tmpdir(), 'grokbot-'));
const UUID = '11111111-2222-3333-4444-555555555555';
const KEY = 'sand.client.slice.account.acme.transcript.replicas.' + UUID;
const put = (name, body) => { const p = join(dir, name); writeFileSync(p, body); return p; };
const blob = (entries) => put(b32(KEY), JSON.stringify({ schemaVersion: 1, value: { entries } }));
const names = new Map([[UUID, 'Pal']]);
const RID = 'ab'.repeat(32);

test('base32 round trip and bad names', () => {
  assert.equal(decodeKey(b32(KEY)), KEY);
  assert.equal(decodeKey('nothex!.blob'), '');
  assert.equal(decodeKey('abc.txt'), '');
  assert.equal(conversationId(KEY), UUID);
  assert.equal(conversationId('other.key'), null);
});

test('roster map', () => {
  const p = put('roster', JSON.stringify({ value: { rows: [{ id: 'a', name: 'A' }, { id: 'b' }] } }));
  assert.deepEqual([...readRoster(p)], [['a', 'A']]);
  assert.equal(readRoster(join(dir, 'missing')).size, 0);
});

test('mappings, ordering, seq filter, streaming', () => {
  const p = blob([
    { kind: 'send-message', seq: 3, timestampMs: 3, message: { type: 'text', content: 'hi' } },
    { kind: 'message', seq: 1, timestampMs: 1, role: 'user', content: 'q' },
    { kind: 'message', seq: 2, timestampMs: 2, role: 'assistant', isStreaming: true, content: 'par' },
    { kind: 'message', seq: 4, timestampMs: 4, role: 'assistant', isStreaming: false, content: 'done' },
    { kind: 'send-message', seq: 5, timestampMs: 5, message: { type: 'text', content: '  ' } },
    { kind: 'send-message', seq: 6, timestampMs: 6, message: { type: 'local-tool-permission',
      ask: { action: 'run-command', target: 'ls', status: 'allowed' } } },
    { kind: 'notice', seq: 7, timestampMs: 7 },
    { kind: 'send-message', seq: 8, timestampMs: 8, message: { type: 'local-tool-permission',
      ask: { requestId: RID, action: 'run-command', target: 'echo hi', status: 'pending' } } },
    { kind: 'send-message', seq: 9, timestampMs: 9, message: { type: 'local-tool-permission',
      ask: { requestId: RID, action: 'run-command', target: 'echo hi', status: 'expired' } } },
    { kind: 'send-message', seq: 10, timestampMs: 10, message: { type: 'local-tool-permission',
      ask: { requestId: RID, action: 'run-command', target: 'echo hi', status: 'bogus' } } },
  ]);
  const r = eventsSince(p, -1, names);
  assert.deepEqual(r.events.map((e) => e.hook_event_name), ['UserPromptSubmit', 'Stop', 'Stop', 'PreToolUse',
    'GrokPermissionPending', 'GrokPermissionResolved']);
  assert.deepEqual(r.events[4], { hook_event_name: 'GrokPermissionPending', tool_name: 'run-command',
    tool_input: { command: 'echo hi' }, request_id: RID, session_id: 'grok-bot:' + UUID, cwd: '/grok/Pal' });
  assert.equal(r.events[5].request_id, RID);
  assert.equal(r.events[1].last_assistant_message, 'hi');
  assert.equal(r.events[2].last_assistant_message, 'done');
  assert.deepEqual(r.events[3].tool_input, { command: 'ls' });
  assert.equal(r.events[3].tool_name, 'run-command');
  assert.equal(r.events[0].session_id, 'grok-bot:' + UUID);
  assert.equal(r.events[0].cwd, '/grok/Pal');
  assert.equal(r.lastSeq, 10);
  const r2 = eventsSince(p, 4, names);
  assert.equal(r2.events.length, 3);
  assert.equal(r2.events[0].hook_event_name, 'PreToolUse');
  assert.equal(eventsSince(p, 10, names).lastSeq, 10);
  assert.equal(eventsSince(p, -1).events[0].cwd, '/grok/Grok Bot');
});

test('truncated JSON does not throw', () => {
  const p = put(b32(KEY), '{"value":{"entries":[{"kind":"mess');
  assert.deepEqual(eventsSince(p, 5, names), { events: [], lastSeq: 5 });
});
