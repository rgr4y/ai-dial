import assert from 'node:assert/strict';
import net from 'node:net';
import { test } from 'node:test';
import { cleanText, Decoder, encode, openStt, registerVoiceMessages, typeIntoTty } from './voice.mjs';

test('encode / decode round trip, split across chunks', () => {
  const pcm = Buffer.from([1, 2, 3, 4]);
  const wire = Buffer.concat([encode('audio-chunk', { rate: 16000 }, pcm), encode('audio-stop')]);
  const d = new Decoder(), got = [];
  for (let i = 0; i < wire.length; i += 3) got.push(...d.push(wire.subarray(i, i + 3)));
  assert.deepEqual(got.map((e) => e.type), ['audio-chunk', 'audio-stop']);
  assert.equal(got[0].data.rate, 16000);
  assert.deepEqual([...got[0].payload], [1, 2, 3, 4]);
  assert.equal(got[1].payload, null);
});

test('openStt: partials, then the final after audio-stop', async () => {
  const seen = [];
  const srv = net.createServer((c) => {
    const d = new Decoder();
    c.on('error', () => {});
    c.on('data', (b) => {
      for (const e of d.push(b)) {
        seen.push(e.type);
        if (e.type === 'audio-chunk') c.write(encode('transcript-chunk', { text: 'fix the' }));
        if (e.type === 'audio-stop') c.write(encode('transcript', { text: 'fix the test' }));
      }
    });
  });
  await new Promise((r) => srv.listen(0, '127.0.0.1', r));
  const partials = [];
  const stt = openStt({ port: srv.address().port, onPartial: (t) => partials.push(t) });
  stt.audio(Buffer.alloc(320));
  await new Promise((r) => setTimeout(r, 50));
  assert.equal(await stt.finish(), 'fix the test');
  assert.deepEqual(partials, ['fix the']);
  assert.deepEqual(seen, ['transcribe', 'audio-start', 'audio-chunk', 'audio-stop']);
  srv.close();
});

test('openStt: a server that only sends the final (batch STT) works; an early final is kept', async () => {
  const srv = net.createServer((c) => { c.on('error', () => {}); c.on('data', () => c.write(encode('transcript', { text: 'early' }))); });
  await new Promise((r) => srv.listen(0, '127.0.0.1', r));
  const stt = openStt({ port: srv.address().port });
  stt.audio(Buffer.alloc(320));
  await new Promise((r) => setTimeout(r, 50));
  assert.equal(await stt.finish(), 'early');
  srv.close();
});

test('openStt: refused connection and silence reject', async () => {
  await assert.rejects(openStt({ port: 1 }).finish());
  const srv = net.createServer(() => {});
  await new Promise((r) => srv.listen(0, '127.0.0.1', r));
  await assert.rejects(openStt({ port: srv.address().port, timeoutMs: 100 }).finish(), /timeout/);
  srv.close();
});

test('voice messages are registered with the API library', () => {
  const pb = registerVoiceMessages();
  assert.equal(pb.VoiceAssistantRequest.id, 90);
  const m = new pb.VoiceAssistantResponse();
  m.setPort(0);
  assert.ok(m.serializeBinary());
});

test('typing refuses a bad tty or empty text without touching iTerm', async () => {
  assert.equal(await typeIntoTty('../x', 'hi'), false);
  assert.equal(await typeIntoTty('ttys003', '  \n '), false);
  assert.equal(cleanText(' a\nb\t c '), 'a b c');
});
