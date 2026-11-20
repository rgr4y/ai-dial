import assert from 'node:assert/strict';
import { afterEach, beforeEach, test, mock } from 'node:test';
import { ASK, ASK_ARM_MS, createVoiceFlow } from './voiceflow.mjs';

const PLAYING = 2, IDLE = 1;
const SESSIONS = [{ id: 'a', name: 'api' }, { id: 'b', name: 'auto-ai' }];
const tick = () => new Promise((r) => setImmediate(r));  // timers are mocked, setImmediate is not

// A flow with every dependency faked. o.route: the router's answer; o.words / o.sttFail: what the STT gives back.
function make(o = {}) {
  const t = { logs: [], said: [], typed: [], tmux: [], answered: [], announced: [], routed: [], aborted: 0, ttys: { a: '/dev/ttys001', b: '/dev/ttys002' },
              words: 'run the tests', sttFail: false, tmuxUp: true, synthFail: false, prompt: null, ...o };
  const dial = { host: 'd1', ready: true, playerKey: 1, playerState: IDLE, ui: { sid: 'a' }, stt: null, pending: null, ask: null, onPlayer: null };
  const board = { tty: (sid) => t.ttys[sid], sessions: new Map(t.prompt ? [['a', { prompt: t.prompt }]] : []) };
  const flow = createVoiceFlow({
    dials: [dial], board, PLAYING, log: (...a) => t.logs.push(a.join(' ')), say: (_d, x) => t.said.push(x),
    sessionList: () => SESSIONS,
    route: async (x) => (t.routed.push(x), typeof t.route === 'function' ? t.route(x) : t.route),
    routerEnv: { k: 1 }, routerTimeoutMs: 123,
    openStt: () => ({ abort: () => t.aborted++, finish: async () => { if (t.sttFail) throw new Error('stt down'); return t.words; } }),
    cleanText: (s) => String(s ?? '').trim(),
    startResponse: () => {}, announce: (_d, url) => t.announced.push(url),
    localAddress: () => '10.0.0.2', ttsUrl: (a, id) => `http://${a}/tts/${id}.wav`,
    synthesize: async () => { if (t.synthFail) throw new Error('tts down'); return Buffer.alloc(44 + 32000); }, stashTts: () => 'w1', wavMs: () => 1000,
    typeIntoTty: async (tty, text) => (t.typed.push([tty, text]), true),
    typeIntoTmuxSession: async (name, text) => (t.tmux.push([name, text]), true),
    tmuxHasSession: async () => t.tmuxUp, assistant: 'voice', chat: async (x) => (t.chatted = x, t.chat ?? { why: 'no key' }),
    answerPrompt: (sid, pid, dec) => t.answered.push([sid, pid, dec]),
  });
  // One hold: start, words arrive, release; resolves when routing is done.
  const hold = (words = t.words) => { t.words = words; flow.voiceStart(dial); return flow.voiceStop(dial); };
  const ui = (v) => { dial.ui = { ...dial.ui, voice: v }; flow.onUiForVoice(dial); };
  const play = () => { dial.onPlayer(PLAYING); dial.onPlayer(IDLE); };  // an announcement runs and ends
  return { t, dial, flow, hold, ui, play };
}
const sent = (m) => { m.ui('wait'); m.ui('confirm'); m.ui('send'); return tick(); };
const logs = (m) => m.t.logs.join('\n');

beforeEach(() => mock.timers.enable({ apis: ['setTimeout', 'Date'], now: 1000 }));
afterEach(() => mock.timers.reset());

test('focused send: confirm card shows the text, send types it into the focused tty, logs', async () => {
  const m = make({ route: { action: 'send', sid: 'a', text: 'run the tests', via: 'rule' } });
  await m.hold();
  assert.deepEqual(m.t.said, [{ final: 'run the tests' }]);
  assert.deepEqual(m.t.routed[0], { text: 'run the tests', sessions: SESSIONS, focusedId: 'a', env: { k: 1 }, timeoutMs: 123 });
  assert.deepEqual(m.t.typed, []);
  await sent(m);
  assert.deepEqual(m.t.typed, [['/dev/ttys001', 'run the tests']]);
  const l = logs(m);
  for (const s of ['voice: listening (d1)', 'voice: heard 13 chars', 'voice: router rule -> send', 'voice: confirm -> api (13 chars)',
                   'voice: dial d1 - -> wait', 'voice: dial d1 wait -> confirm (pending)', 'voice: dial d1 confirm -> send (pending)', 'voice: typed 13 chars into /dev/ttys001'])
    assert.ok(l.includes(s), s);
});
test('named send to a non-focused session: card shows "name: text", types into that tty', async () => {
  const m = make({ route: { action: 'send', sid: 'b', text: 'ship it', via: 'rule' } });
  await m.hold('tell auto ai to ship it');
  assert.deepEqual(m.t.said, [{ final: 'auto-ai: ship it' }]);
  await sent(m);
  assert.deepEqual(m.t.typed, [['/dev/ttys002', 'ship it']]);
});
test('confirm -> off (tap cancel) drops the pending send', async () => {
  const m = make({ route: { action: 'send', sid: 'a', text: 'x', via: 'rule' } });
  await m.hold();
  m.ui('wait'); m.ui('confirm'); m.ui('off'); await tick();
  assert.equal(m.dial.pending, null);
  m.ui('send'); await tick();
  assert.deepEqual(m.t.typed, []);
});
test('confirm -> listen (new hold) drops the pending send', async () => {
  const m = make({ route: { action: 'send', sid: 'a', text: 'x', via: 'rule' } });
  await m.hold();
  m.ui('wait'); m.ui('confirm'); m.ui('listen');
  assert.equal(m.dial.pending, null);
  m.ui('send'); await tick();
  assert.deepEqual(m.t.typed, []);
});
test('a new voiceStart drops the pending send and aborts a stt still open', async () => {
  const m = make({ route: { action: 'send', sid: 'a', text: 'x', via: 'rule' } });
  await m.hold();
  m.flow.voiceStart(m.dial);
  assert.equal(m.dial.pending, null);
  m.flow.voiceStart(m.dial);  // the second start finds the first one's stt still open
  assert.equal(m.t.aborted, 1);
});
test('send with no tty known (VS Code session): the assistant answers instead', async () => {
  const m = make({ route: { action: 'send', sid: 'a', text: 'x', via: 'rule' }, ttys: {}, tmuxUp: false, chat: { text: 'Hi.' } });
  await m.hold();
  assert.equal(m.t.chatted, 'x');
  assert.deepEqual(m.t.said, [{ final: 'Hi.' }]);
  await sent(m);
  assert.deepEqual(m.t.typed, []);
  assert.ok(logs(m).includes('voice: no terminal known for session a, to the assistant'));
});
test('nothing focused and no session has a terminal: the assistant, not "Which session?"', async () => {
  const m = make({ route: { action: 'send', sid: undefined, text: 'x', via: 'model' }, ttys: {}, tmuxUp: false, chat: { text: 'Hi.' } });
  await m.hold();
  assert.deepEqual(m.t.said, [{ final: 'Hi.' }]);
});
test('empty transcript: "Didn\'t catch that", router not called', async () => {
  const m = make({ route: { action: 'send', sid: 'a', text: 'x' } });
  await m.hold('   ');
  assert.deepEqual(m.t.said, [{ error: "Didn't catch that" }]);
  assert.equal(m.t.routed.length, 0);
  assert.ok(logs(m).includes('voice: heard nothing'));
});
test('STT failure: "Speech service offline"', async () => {
  const m = make({ sttFail: true });
  await m.hold();
  assert.deepEqual(m.t.said, [{ error: 'Speech service offline' }]);
  assert.ok(logs(m).includes('voice: failed: stt down'));
});
test('voiceStop with no hold in progress does nothing', () => {
  const m = make();
  assert.equal(m.flow.voiceStop(m.dial), undefined);
  assert.deepEqual(m.t.said, []);
});
test('assistant route: tmux session present -> typed into it on send', async () => {
  const m = make({ route: { action: 'assistant', text: 'what is up', via: 'model', why: 'chat' } });
  await m.hold();
  assert.deepEqual(m.t.said, [{ final: 'Assistant: what is up' }]);
  await sent(m);
  assert.deepEqual(m.t.tmux, [['voice', 'what is up']]);
  assert.ok(logs(m).includes('voice: router model (chat) -> assistant'));
  assert.ok(logs(m).includes('voice: typed 10 chars into assistant'));
});
test('assistant route: tmux absent, built-in down -> "Assistant offline", nothing pending', async () => {
  const m = make({ route: { action: 'assistant', text: 'hi', via: 'model' }, tmuxUp: false });
  await m.hold();
  assert.deepEqual(m.t.said, [{ error: 'Assistant offline' }]);
  assert.equal(m.dial.pending, null);
  assert.ok(logs(m).includes('voice: built-in assistant failed: no key'));
});
test('assistant route: tmux absent -> built-in answer on the card, spoken on send', async () => {
  const m = make({ route: { action: 'assistant', text: 'what time is it in tokyo', via: 'rule' }, tmuxUp: false, chat: { text: 'It is 3 AM.' } });
  await m.hold();
  assert.equal(m.t.chatted, 'what time is it in tokyo');
  assert.deepEqual(m.t.said, [{ final: 'It is 3 AM.' }]);
  await sent(m);
  await tick();
  assert.equal(m.t.announced.length, 1);
  assert.ok(logs(m).includes('voice: built-in assistant replied 11 chars'));
});
test('allow route answers the open prompt on send; deny too', async () => {
  for (const [decision, label] of [['allow', 'Allow'], ['deny', 'Deny']]) {
    const m = make({ route: { action: 'answer', sid: 'a', decision, via: 'rule' }, prompt: { id: 'p1', tool: 'Bash' } });
    await m.hold('yes');
    assert.deepEqual(m.t.said, [{ final: `${label} api: Bash` }]);
    await sent(m);
    assert.deepEqual(m.t.answered, [['a', 'p1', decision]]);
  }
});
test('answer route with no prompt on the board: card says "prompt", the answer is passed with no prompt id (bridge drops it)', async () => {
  const m = make({ route: { action: 'answer', sid: 'a', decision: 'allow', via: 'rule' } });
  await m.hold('yes');
  assert.deepEqual(m.t.said, [{ final: 'Allow api: prompt' }]);
  await sent(m);
  assert.deepEqual(m.t.answered, [['a', undefined, 'allow']]);
});
test('speak reply route: card shows the reply, send speaks it', async () => {
  const m = make({ route: { action: 'speak', text: 'two sessions busy', via: 'model' } });
  await m.hold('status?');
  assert.deepEqual(m.t.said, [{ final: 'two sessions busy' }]);
  assert.deepEqual(m.t.announced, []);
  await sent(m);
  assert.deepEqual(m.t.announced, ['http://10.0.0.2/tts/w1.wav']);
});
test('speak reply route: a failing speak is logged, not thrown', async () => {
  const m = make({ route: { action: 'speak', text: 'x', via: 'model' }, synthFail: true });
  await m.hold();
  await sent(m);
  assert.ok(logs(m).includes('voice: speak failed: tts down'));
});
test('router throwing reports "Speech service offline"', async () => {
  const m = make({ route: () => { throw new Error('boom'); } });
  await m.hold();
  assert.deepEqual(m.t.said, [{ error: 'Speech service offline' }]);
});

// ---- ask flow ----
const askHold = async (m) => {
  m.t.route = { action: 'send', sid: null, text: 'fix the build', via: 'model' };
  await m.hold('fix the build');
  await tick();
};
test('ask: no sid -> speaks "Which session?", the dial gets {ask} only after playback completes', async () => {
  const m = make();
  await askHold(m);
  assert.equal(m.dial.ask.text, 'fix the build');
  assert.equal(m.t.announced.length, 1);
  assert.deepEqual(m.t.said, []);
  m.dial.onPlayer(PLAYING); await tick();
  assert.deepEqual(m.t.said, []);  // still playing
  m.dial.onPlayer(IDLE); await tick();
  assert.deepEqual(m.t.said, [{ ask: ASK }]);
  assert.equal(m.dial.ask.armed, true);
});
test('ask: answer matches one session -> confirm card with the ORIGINAL text, send types it there', async () => {
  const m = make();
  await askHold(m); m.play(); await tick();
  m.flow.voiceStart(m.dial);  // the armed re-listen
  assert.ok(m.dial.ask, 'armed re-listen keeps the question');
  m.t.words = 'Auto AI';
  await m.flow.voiceStop(m.dial);
  assert.deepEqual(m.t.said.at(-1), { final: 'auto-ai: fix the build' });
  assert.equal(m.t.routed.length, 1);  // the answer is not routed
  assert.ok(logs(m).includes('(answer to ask)'));
  await sent(m);
  assert.deepEqual(m.t.typed, [['/dev/ttys002', 'fix the build']]);
});
test('ask: answer matches none -> "No session called X"', async () => {
  const m = make();
  await askHold(m); m.play(); await tick();
  await m.hold('nothing');
  assert.deepEqual(m.t.said.at(-1), { error: 'No session called nothing' });
  assert.equal(m.dial.ask, null);
  assert.ok(logs(m).includes('voice: no session called "nothing" (0 matches), dropped'));
});
test('ask: empty answer -> "Didn\'t catch that", question ended', async () => {
  const m = make();
  await askHold(m); m.play(); await tick();
  await m.hold('');
  assert.deepEqual(m.t.said.at(-1), { error: "Didn't catch that" });
  assert.equal(m.dial.ask, null);
});
test('ask: a new hold before it is armed drops the question', async () => {
  const m = make();
  await askHold(m);
  m.flow.voiceStart(m.dial);  // user grabs the dial while "Which session?" is still playing
  assert.equal(m.dial.ask, null);
  m.play(); await tick();
  assert.deepEqual(m.t.said, []);  // no {ask} for a dropped question
});
test('ask: the armed re-listen keeps it, and a second start is a new hold', async () => {
  const m = make();
  await askHold(m); m.play(); await tick();
  m.flow.voiceStart(m.dial);
  assert.equal(m.dial.ask.armed, false);  // arm spent
  m.flow.voiceStart(m.dial);
  assert.equal(m.dial.ask, null);
});
test('ask: the arm lapses after ASK_ARM_MS, a later hold routes normally', async () => {
  const m = make();
  await askHold(m); m.play(); await tick();
  mock.timers.tick(ASK_ARM_MS + 1);
  m.t.route = { action: 'send', sid: 'a', text: 'api', via: 'rule' };
  await m.hold('api');
  assert.equal(m.t.routed.length, 2);
  assert.deepEqual(m.t.said.at(-1), { final: 'api' });  // routed as new words, not taken as a session name
});
test('ask: speak failure still arms', async () => {
  const m = make({ synthFail: true });
  await askHold(m);
  assert.deepEqual(m.t.said, [{ ask: ASK }]);
  assert.equal(m.dial.ask.armed, true);
  assert.ok(logs(m).includes('voice: ask speak failed: tts down'));
});

// ---- speak queue ----
test('speak: two overlapping calls run one after the other', async () => {
  const m = make();
  const p1 = m.flow.speak(m.dial, 'one'), p2 = m.flow.speak(m.dial, 'two');
  await tick();
  assert.equal(m.t.announced.length, 1);
  m.dial.onPlayer(PLAYING); m.dial.onPlayer(IDLE);
  assert.equal(await p1, true);
  await tick();
  assert.equal(m.t.announced.length, 2);
  m.play();
  assert.equal(await p2, true);
});
test('speak: a playing earcon is waited out before the announcement counts', async () => {
  const m = make();
  m.dial.playerState = PLAYING;
  let done = null;
  const p = m.flow.speak(m.dial, 'hi').then((v) => (done = v));
  await tick();
  assert.equal(m.t.announced.length, 0);  // waiting for the earcon
  m.dial.onPlayer(IDLE);  // the earcon's end
  await tick();
  assert.equal(m.t.announced.length, 1);
  assert.equal(done, null);  // that IDLE was not the announcement's
  m.dial.onPlayer(PLAYING); m.dial.onPlayer(IDLE);
  await p;
  assert.equal(done, true);
});
test('speak: earcon that never ends is given up on after 3 s', async () => {
  const m = make();
  m.dial.playerState = PLAYING;
  m.flow.speak(m.dial, 'hi');
  await tick();
  mock.timers.tick(3000); await tick();
  assert.equal(m.t.announced.length, 1);
});
test('speak: never started (5 s) resolves false', async () => {
  const m = make();
  const p = m.flow.speak(m.dial, 'hi');
  await tick();
  mock.timers.tick(5000);
  assert.equal(await p, false);
  assert.ok(logs(m).includes('speak: d1 never started announcing'));
});
test('speak: disconnect (null state) resolves false', async () => {
  const m = make();
  const p = m.flow.speak(m.dial, 'hi');
  await tick();
  m.dial.onPlayer(PLAYING); m.dial.onPlayer(null);
  assert.equal(await p, false);
});
test('speak: announcement that starts but never finishes resolves false after wav + 15 s', async () => {
  const m = make();
  const p = m.flow.speak(m.dial, 'hi');
  await tick();
  m.dial.onPlayer(PLAYING);
  mock.timers.tick(16000);
  assert.equal(await p, false);
  assert.ok(logs(m).includes('speak: d1 announcement did not finish'));
});
test('speak: no ready dial with a player -> false', async () => {
  const m = make();
  m.dial.ready = false;
  assert.equal(await m.flow.speak(null, 'hi'), false);
  assert.ok(logs(m).includes('speak: no dial with a media player'));
});
test('speak(null) picks the first ready dial with a player', async () => {
  const m = make();
  const p = m.flow.speak(null, 'hi');
  await tick();
  assert.equal(m.t.announced.length, 1);
  m.play();
  assert.equal(await p, true);
});
test('speak: a failing announce send rejects', async () => {
  const m = make();
  const flow = createVoiceFlow({ ...{}, dials: [m.dial], board: {}, PLAYING, log() {}, say() {}, localAddress: () => 'x', ttsUrl: () => 'u',
    synthesize: async () => Buffer.alloc(44), stashTts: () => 'w', wavMs: () => 0, announce: () => { throw new Error('link dead'); } });
  await assert.rejects(flow.speak(m.dial, 'hi'), /link dead/);
});

// ---- dial voice state ----
test('voice "send" seen on consecutive ui updates runs the pending action exactly once', async () => {
  const m = make({ route: { action: 'send', sid: 'a', text: 'once', via: 'rule' } });
  await m.hold();
  m.ui('wait'); m.ui('confirm'); m.ui('send'); m.ui('send'); m.ui('send'); await tick();
  assert.deepEqual(m.t.typed, [['/dev/ttys001', 'once']]);
});
test('"send" with nothing pending is ignored', async () => {
  const m = make();
  m.ui('send'); await tick();
  assert.deepEqual(m.t.typed, []);
});
test('voiceStart: a failing start response is logged and no stt opens', () => {
  const m = make();
  const f = createVoiceFlow({ dials: [], board: {}, PLAYING, log: (...a) => m.t.logs.push(a.join(' ')), say() {}, startResponse: () => { throw new Error('gone'); }, openStt: () => assert.fail('no stt') });
  f.voiceStart(m.dial);
  assert.ok(logs(m).includes('voice start on d1 failed: gone'));
});
