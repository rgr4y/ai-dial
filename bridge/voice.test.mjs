import assert from 'node:assert/strict';
import net from 'node:net';
import { test } from 'node:test';
import { applySpeakRules, cleanText, Decoder, encode, loadRules, openStt, registerVoiceMessages, stashTts, synthesize, ttsFiles, typeIntoTty, wavMs } from './voice.mjs';

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

// ---- speaking ----
// A fake Wyoming TTS: on synthesize it answers audio-start, two audio-chunks of `n` samples each (a ramp), audio-stop.
async function fakeTts({ rate = 16000, channels = 1, n = 800, width = 2, silent = false } = {}) {
  const asked = [];
  const srv = net.createServer((c) => {
    const d = new Decoder();
    c.on('error', () => {});
    c.on('data', (b) => {
      for (const e of d.push(b)) {
        if (e.type !== 'synthesize') continue;
        asked.push(e.data.text);
        if (silent) return;
        const fmt = { rate, width, channels };
        const pcm = Buffer.alloc(n * channels * 2);
        for (let i = 0; i < n * channels; i++) pcm.writeInt16LE(i % 100, i * 2);
        c.write(Buffer.concat([encode('audio-start', fmt), encode('audio-chunk', fmt, pcm), encode('audio-chunk', fmt, pcm), encode('audio-stop')]));
      }
    });
  });
  await new Promise((r) => srv.listen(0, '127.0.0.1', r));
  return { srv, asked, port: srv.address().port };
}

test('synthesize: collects the chunks into a 16 kHz mono WAV; rules run before the server sees the text', async () => {
  const { srv, asked, port } = await fakeTts();
  const wav = await synthesize('Run `npm test` in the api dir', { port, rules: loadRules() });
  assert.deepEqual(asked, ['Run N P M test in the A P I dir']);
  assert.equal(wav.subarray(0, 4).toString(), 'RIFF');
  assert.equal(wav.readUInt32LE(24), 16000);
  assert.equal(wav.readUInt16LE(22), 1);
  assert.equal(wav.readUInt16LE(34), 16);
  assert.equal(wav.length, 44 + 800 * 2 * 2);
  assert.equal(wavMs(wav), 100);
  srv.close();
});

test('synthesize: resamples and downmixes a server that sends another format', async () => {
  const { srv, port } = await fakeTts({ rate: 22050, channels: 2, n: 2205 });
  const wav = await synthesize('hello', { port, rules: [] });
  assert.equal(wav.readUInt32LE(24), 16000);
  assert.equal(wav.length, 44 + Math.floor((2205 * 2 * 16000) / 22050) * 2);  // 2 chunks, mono, 16 kHz
  srv.close();
});

test('synthesize: unspeakable text is null without a connection; 24-bit audio, silence and refusal reject', async () => {
  assert.equal(await synthesize(' ... ', { port: 1, rules: loadRules() }), null);
  const w = await fakeTts({ width: 3 });
  await assert.rejects(synthesize('hi', { port: w.port, rules: [] }), /width/);
  w.srv.close();
  const s = await fakeTts({ silent: true });
  await assert.rejects(synthesize('hi', { port: s.port, rules: [], timeoutMs: 100 }), /timeout/);
  s.srv.close();
  await assert.rejects(synthesize('hi', { port: 1, rules: [] }));
});

test('speak rules: claude-speak filter behaviour', () => {
  const r = loadRules();
  assert.ok(r.length > 20, 'claude-speak speak-rules.json found');
  assert.equal(applySpeakRules('See https://x.dev/a and `foo_bar.py` now', r), 'See https:/x dot dev a and foo bar dot py now');
  assert.equal(applySpeakRules('Which session? esphome on 4 GB', r), 'Which session? E S P Home on 4 gigabytes');
  assert.equal(applySpeakRules('# Title\n', [{ type: 'replace_re', pattern: '^#{1,6}\\s+', replacement: '' }]), 'Title');
  assert.equal(applySpeakRules('a-b', [{ type: 'replace_re', pattern: '(a)-(b)', replacement: '\\2 \\1' }]), 'b a');
});

test('tts files are served from the stash and expire', async () => {
  const id = stashTts(Buffer.from('x'), 20);
  assert.ok(ttsFiles.has(id));
  await new Promise((r) => setTimeout(r, 60));
  assert.ok(!ttsFiles.has(id));
});

test('media player announcement message serialises', () => {
  const pb = registerVoiceMessages();
  const m = new pb.MediaPlayerCommandRequest();
  m.setKey(7); m.setHasMediaUrl(true); m.setMediaUrl('http://10.0.0.2:19877/tts/ab.wav'); m.setHasAnnouncement(true); m.setAnnouncement(true);
  assert.ok(m.serializeBinary().length > 10);
  assert.equal(pb.MediaPlayerState.MEDIA_PLAYER_STATE_ANNOUNCING, 4);
});
