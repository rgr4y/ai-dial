// Host check for the Copilot PreToolUse gate in hook.sh: mutating tools gate, reads pass through (busy only), and
// both payload shapes normalise. Runs hook.sh against a stub daemon on a port.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createServer } from 'node:http';
import { spawn } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { dirname, join } from 'node:path';

const HOOK = join(dirname(fileURLToPath(import.meta.url)), 'hook.sh');

// Stub daemon: records /event posts, answers /permission with `decision` (or holds if undecided).
function stub(decision) {
  return new Promise((resolve) => {
    const seen = { events: [], prompts: [] };
    const srv = createServer((req, res) => {
      let b = '';
      req.on('data', (c) => (b += c));
      req.on('end', () => {
        const ev = JSON.parse(b || '{}');
        if (req.url === '/event') { seen.events.push(ev); res.writeHead(204).end(); return; }
        if (req.url === '/permission') {
          seen.prompts.push(ev);
          if (!decision) return;  // hold open (the caller kills us)
          res.writeHead(200, { 'content-type': 'application/json' }).end(JSON.stringify({ decision }));
          return;
        }
        res.writeHead(404).end();
      });
    });
    srv.listen(0, '127.0.0.1', () => resolve({ seen, port: srv.address().port, close: () => srv.close() }));
  });
}

// Run hook.sh with an argv and a stdin payload; resolve {code, out}.
function run(argv, payload, port) {
  return new Promise((resolve) => {
    const p = spawn('bash', [HOOK, ...argv], { env: { ...process.env, DIAL_BRIDGE_PORT: String(port) } });
    let out = '', err = '';
    p.stdout.on('data', (c) => (out += c));
    p.stderr.on('data', (c) => (err += c));
    p.on('close', (code) => resolve({ code, out, err }));
    p.stdin.end(JSON.stringify(payload));
  });
}

test('copilot-gate: a mutating tool gates and returns the dial decision as permissionDecision', async () => {
  const s = await stub('allow');
  try {
    const { code, out } = await run(['copilot-gate'], { hook_event_name: 'PreToolUse', session_id: 's', tool_name: 'bash', tool_input: { command: 'ls' } }, s.port);
    assert.equal(code, 0);
    assert.deepEqual(JSON.parse(out), { hookSpecificOutput: { hookEventName: 'PreToolUse', permissionDecision: 'allow' } });
    assert.equal(s.seen.events.length, 1);          // the busy event was posted too
    assert.equal(s.seen.events[0].agent, 'copilot'); // tagged, so the dial shows the Copilot pet
    assert.equal(s.seen.prompts.length, 1);          // and the prompt was opened for the dial
    assert.equal(s.seen.prompts[0].tool_name, 'bash');
  } finally { s.close(); }
});

test('copilot-gate: a deny comes back as permissionDecision deny', async () => {
  const s = await stub('deny');
  try {
    const { out } = await run(['copilot-gate'], { hook_event_name: 'PreToolUse', session_id: 's', tool_name: 'apply_patch', tool_input: {} }, s.port);
    const j = JSON.parse(out);
    assert.equal(j.hookSpecificOutput.permissionDecision, 'deny');
  } finally { s.close(); }
});

test('copilot-gate: a read tool posts the busy event but does not open a prompt', async () => {
  const s = await stub('allow');
  try {
    const { code, out } = await run(['copilot-gate'], { hook_event_name: 'PreToolUse', session_id: 's', tool_name: 'view', tool_input: { path: 'x' } }, s.port);
    assert.equal(code, 0);
    assert.equal(out, '');                    // no decision printed: Copilot decides as normal
    assert.equal(s.seen.prompts.length, 0);
    assert.equal(s.seen.events.length, 1);
  } finally { s.close(); }
});

test('copilot-gate: the CLI camelCase shape normalises and gates', async () => {
  const s = await stub('allow');
  try {
    const { out } = await run(['copilot-gate'], { hookType: 'preToolUse', sessionId: 's', toolCalls: [{ name: 'bash', args: { command: 'ls' } }] }, s.port);
    assert.equal(JSON.parse(out).hookSpecificOutput.permissionDecision, 'allow');
    assert.equal(s.seen.prompts[0].tool_name, 'bash');
    assert.deepEqual(s.seen.prompts[0].tool_input, { command: 'ls' });
  } finally { s.close(); }
});

test('copilot-gate: no daemon at all fails open with no output', async () => {
  const { code, out } = await run(['copilot-gate'], { hook_event_name: 'PreToolUse', session_id: 's', tool_name: 'bash', tool_input: {} }, 1 /* unused port */);
  assert.equal(code, 0);
  assert.equal(out, '');
});
