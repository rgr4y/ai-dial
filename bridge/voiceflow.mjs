// The dial's voice flow: hold -> STT -> route -> confirm card -> send, the "Which session?" question, and the speak queue.
// dial-bridge.mjs wires the real board / router / iTerm / TTS / protobuf in; tests inject fakes (voiceflow.test.mjs).
//
// deps:
//   dials                 the dial list (speak(null, ...) picks the first ready one with a media player)
//   board                 .tty(sid), .sessions (Map: sid -> { prompt })
//   say(dial, obj)        a {evt:'voice'} line to that dial
//   sessionList(board), route(opts), routerEnv, routerTimeoutMs
//   openStt(onPartial)    -> { audio, finish(), abort() }; cleanText(raw)
//   startResponse(dial)   sends VoiceAssistantResponse (throws on a dead link)
//   announce(dial, url)   sends the media_player announcement command (throws on a dead link)
//   localAddress(dial)    our address as the dial reaches us, or undefined
//   ttsUrl(addr, id)      URL of a stashed WAV; synthesize(text), stashTts(wav), wavMs(wav)
//   PLAYING               the media player state that means "announcing"
//   typeIntoTty, typeIntoTmuxSession, tmuxHasSession, assistant (tmux session name)
//   chat(text)            -> { text } | { why }: the built-in assistant when the tmux one isn't running
//   answerPrompt(sid, promptId, decision), log
export const ASK = 'Which session?', ASK_ARM_MS = 3000;  // the dial starts the re-listen well inside this

export function createVoiceFlow(d) {
  const { dials, board, say, log, PLAYING } = d;

  function voiceStart(dial) {
    dial.stt?.abort();
    dial.pending = null;
    // A new hold drops a question; the re-listen it armed is not one. The arm is spent by that one start, and lapses if
    // the dial never started it (mic failed), so a later hold is routed normally, not taken as a session name.
    if (dial.ask?.armed && Date.now() - dial.ask.armedAt < ASK_ARM_MS) dial.ask.armed = false;
    else dial.ask = null;
    try {
      d.startResponse(dial);  // port 0: audio comes over the API connection
    } catch (e) {
      return log(`voice start on ${dial.host} failed:`, e.message);
    }
    log(`voice: listening (${dial.host})`);
    dial.stt = d.openStt((text) => say(dial, { text }));
  }

  function voiceStop(dial) {
    const stt = dial.stt;
    dial.stt = null;
    if (!stt) return;
    return stt.finish().then(async (raw) => {
      const text = d.cleanText(raw), focusedId = dial.ui?.sid;
      const ask = dial.ask;
      dial.ask = null;  // any outcome ends the question
      log(`voice: heard ${text ? text.length + ' chars' : 'nothing'}${ask ? ' (answer to ask)' : ''}`);
      if (!text) return say(dial, { error: "Didn't catch that" });
      const sessions = d.sessionList(board);
      if (ask) {  // the answer to "Which session?": a name, then the original words go there
        const key = (s) => String(s).toLowerCase().replace(/[^\p{L}\p{N}]+/gu, '');  // router.mjs find(): exact, ignoring case and punctuation
        const hit = sessions.filter((s) => key(s.name) === key(text));
        if (hit.length !== 1) {
          log(`voice: no session called "${text}" (${hit.length} matches), dropped`);
          return say(dial, { error: `No session called ${text}` });
        }
        return queueSend(dial, hit[0].id, ask.text, sessions, focusedId);
      }
      const r = await d.route({ text, sessions, focusedId, env: d.routerEnv, timeoutMs: d.routerTimeoutMs });
      log(`voice: router ${r.via}${r.why ? ` (${r.why})` : ''} -> ${r.action}`);
      const name = (sid) => sessions.find((s) => s.id === sid)?.name || sid;
      if (r.action === 'send') {
        // Nothing on the face, none named: ask which, if any session can take typing; else the assistant answers.
        if (!r.sid) return sessions.some((x) => board.tty(x.id)) ? askSession(dial, r.text) : toAssistant(dial, r.text);
        return queueSend(dial, r.sid, r.text, sessions, focusedId);
      }
      if (r.action === 'assistant') return toAssistant(dial, r.text);
      if (r.action === 'answer') {
        const p = board.sessions.get(r.sid)?.prompt;
        dial.pending = { run: () => d.answerPrompt(r.sid, p?.id, r.decision) };
        return say(dial, { final: `${r.decision === 'allow' ? 'Allow' : 'Deny'} ${name(r.sid)}: ${p?.tool || 'prompt'}` });
      }
      dial.pending = { run: () => speak(dial, r.text).catch((e) => log('voice: speak failed:', e.message)) };  // the card shows the reply; "send" also says it
      say(dial, { final: r.text });
    }).catch((e) => {
      log('voice: failed:', e.message);
      say(dial, { error: 'Speech service offline' });
    });
  }

  // The tmux "voice" Claude when it runs (typed on send), else the built-in assistant: its answer shows on the card and
  // is spoken on send, like a router "speak".
  async function toAssistant(dial, text) {
    if (await d.tmuxHasSession(d.assistant)) {
      dial.pending = { run: () => d.typeIntoTmuxSession(d.assistant, text).then(
        (ok) => log(ok ? `voice: typed ${text.length} chars into assistant` : 'voice: assistant went away, dropped'),
        (e) => log('voice: assistant tmux failed:', e.message)) };
      return say(dial, { final: `Assistant: ${text}` });
    }
    const r = await d.chat(text);
    if (!r.text) {
      log(`voice: built-in assistant failed: ${r.why}`);
      return say(dial, { error: 'Assistant offline' });
    }
    log(`voice: built-in assistant replied ${r.text.length} chars`);
    dial.pending = { run: () => speak(dial, r.text).catch((e) => log('voice: speak failed:', e.message)) };
    say(dial, { final: r.text });
  }

  // Type text into a session once the confirm card runs out.
  function queueSend(dial, sid, text, sessions, focusedId) {
    if (!board.tty(sid)) {  // only Claude Code in an iTerm tab can be typed into (VS Code, etc. can't): ask the assistant
      log(`voice: no terminal known for session ${sid}, to the assistant`);
      return toAssistant(dial, text);
    }
    dial.pending = { run: () => typeSession(sid, text) };
    log(`voice: confirm -> ${sessions.find((s) => s.id === sid)?.name || sid} (${text.length} chars)`);
    say(dial, { final: sid === focusedId ? text : `${sessions.find((s) => s.id === sid)?.name || sid}: ${text}` });
  }

  function typeSession(sid, text) {
    const tty = board.tty(sid);
    if (!tty) return log(`voice: no terminal known for session ${sid}, dropped`);
    return d.typeIntoTty(tty, text).then(
      (ok) => log(ok ? `voice: typed ${text.length} chars into ${tty}` : `voice: no iTerm session on ${tty}, dropped`),
      (e) => log('voice: osascript failed:', e.message));
  }

  // No session to send to: say "Which session?", and only once it has finished playing (or failed) tell the dial to listen
  // again, so the mic does not hear the question. dial.ask holds the original text until the answer, a new hold, or an
  // empty / unmatched answer; `armed` marks the re-listen's own voiceStart as not a new hold.
  function askSession(dial, text) {
    const ask = dial.ask = { text, armed: false };
    speak(dial, ASK).catch((e) => log('voice: ask speak failed:', e.message)).then(() => {
      if (dial.ask !== ask || !dial.ready) return;  // a new hold took over
      ask.armed = true;
      ask.armedAt = Date.now();
      say(dial, { ask: ASK });
    });
  }

  // Resolves true when the dial reported announcing and then left it (playback done: qr8 re-arms the mic on that), false
  // when it never started / never finished / disconnected (logged).
  function waitPlayed(dial, ms) {
    return new Promise((res) => {
      let started = false;
      const end = (ok) => { clearTimeout(startT); clearTimeout(endT); dial.onPlayer = null; res(ok); };
      const startT = setTimeout(() => { if (!started) { log(`speak: ${dial.host} never started announcing`); end(false); } }, 5000);
      const endT = setTimeout(() => { log(`speak: ${dial.host} announcement did not finish`); end(false); }, ms + 15000);
      dial.onPlayer = (st) => {
        if (st === null) end(false);  // disconnected
        else if (st === PLAYING) started = true;
        else if (started) end(true);
      };
    });
  }
  // Until the player leaves PLAYING (an earcon still going), or ms. Resolves at once if it isn't playing.
  function waitIdle(dial, ms) {
    if (dial.playerState !== PLAYING) return Promise.resolve();
    return new Promise((res) => {
      const end = () => { clearTimeout(t); dial.onPlayer = null; res(); };
      const t = setTimeout(end, ms);
      dial.onPlayer = (st) => { if (st !== PLAYING) end(); };
    });
  }
  // One speak at a time per dial: dial.onPlayer has one slot, and a second announcement's state changes would otherwise
  // end the first one's wait early or leave it to time out.
  function speak(dial, text) {
    dial ??= dials.find((x) => x.ready && x.playerKey != null);
    if (!dial) return log('speak: no dial with a media player'), Promise.resolve(false);
    const run = (dial.speaking ?? Promise.resolve()).then(() => speakNow(dial, text));
    dial.speaking = run.catch(() => {});
    return run;
  }
  async function speakNow(dial, text) {
    const addr = d.localAddress(dial);
    if (!dial?.ready || dial.playerKey == null || !addr) return log('speak: no dial with a media player'), false;
    const wav = await d.synthesize(text);  // no voice: the server's default
    if (!wav) return false;
    // An earcon (the end sound on release) may still be playing: its PLAYING -> IDLE must not count as this one's.
    await waitIdle(dial, 3000);
    const url = d.ttsUrl(addr, d.stashTts(wav));
    const played = waitPlayed(dial, d.wavMs(wav));
    try {
      d.announce(dial, url);
    } catch (e) {
      dial.onPlayer?.(null);
      throw e;
    }
    log(`speak: ${d.wavMs(wav)} ms -> ${dial.host} ${url}`);
    return played;
  }

  // Dial state: "send" runs the pending action; leaving wait / confirm any other way (tap to cancel, timeout) drops it.
  // A "send" with nothing pending (the simulator's fake words) is ignored.
  function onUiForVoice(dial) {
    const v = dial.ui?.voice;
    if (v !== dial.lastVoice) log(`voice: dial ${dial.host} ${dial.lastVoice ?? '-'} -> ${v ?? '-'}${dial.pending ? ' (pending)' : ''}`);
    dial.lastVoice = v;
    if (v === 'wait' || v === 'confirm') return;
    const p = dial.pending;
    dial.pending = null;
    if (v === 'send') p?.run();
  }

  return { voiceStart, voiceStop, speak, onUiForVoice };
}
