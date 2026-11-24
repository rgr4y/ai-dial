# Dial protocol

How agent status gets onto the dial, how the dial reports what it shows, and how taps travel back. Everything is
JSON. There are three hops:

```
agents ──hook JSON──▶ dial-bridge ──lines──▶ dial(s) (ESPHome API: the dial, and the host sim on 127.0.0.1)
                       (Node, :19876)  ◀──ui / decision── dial(s)
                       (:19877 assets) ◀──GET pack files── dial (LittleFS sync)
```

## 1. Agents → bridge (HTTP, 127.0.0.1:19876)

`bridge/hook.sh` POSTs the hook's stdin unchanged. That JSON is Claude Code's hook input:
`hook_event_name`, `session_id`, `cwd`, plus `tool_name`/`tool_input`, `last_assistant_message`, and so on.
peon-ping's adapters (Codex, Cursor, Gemini, …) produce the same shape, and `bridge/peon-shim/peon.sh` tees it here.
`hook.sh` also adds `"tty":"ttys003"` (`ps -o tty= -p $PPID`) when its parent has a terminal. The bridge keeps it per
session (`Board.tty(session_id)`) so voice can type into that terminal; it never goes to the dial.

| Endpoint | Body | Reply |
|---|---|---|
| `POST /event` | any hook JSON | `204` immediately |
| `POST /permission` | a `PermissionRequest` hook JSON | Held open until the dial answers: `200 {"decision":"allow"\|"deny"}`. Otherwise `204`: the dial is offline, the prompt was answered in the terminal (a later event from that session arrived), or it timed out (280 s). |
| `GET /status` | none | `{dial:{host,ready}, heartbeat}` |

`hook.sh` turns `allow`/`deny` into Claude Code's
`{"hookSpecificOutput":{"hookEventName":"PermissionRequest","decision":{"behavior":…}}}`. Every failure prints
nothing, and Claude Code shows its own prompt as usual. It also shows that prompt while the hook waits, and
whichever answer comes first wins.

### Copilot (VS Code) has no PermissionRequest hook

VS Code's Copilot Chat fires only the busy events (its extension calls its hook runner for
`SessionStart`, `UserPromptSubmit`, `PreToolUse`, `PostToolUse`, `Stop`, subagent/precompact) — never a permission
hook: the allow/deny prompt is handled in-process by the extension, not by shelling out. So the dial cannot be the
approver via `PermissionRequest`.

Instead, `hook.sh copilot-gate` (installed for `PreToolUse` in `~/.copilot/hooks/dial-bridge.json`, the location VS
Code's `chat.hookFilesLocations` reads; the CLI's `~/.copilot/settings.json` is a different file) puts the dial in the
loop at `PreToolUse`:

- It always posts the busy event (agent `copilot`).
- For a *mutating* tool (`bash`, `write`, `edit`, `apply_patch`, …) it also holds on `POST /permission` and answers
  `{"hookSpecificOutput":{"hookEventName":"PreToolUse","permissionDecision":"allow"|"deny"}}`. VS Code honours that
  (`resolveAutoConfirmFromHook`): `allow` auto-confirms, `deny` blocks.
- Read-only tools (`view`, `glob`, `rg`, …) pass straight through, so the dial is not prompted for every search.

Payloads differ by surface and the gate normalises both: VS Code's extension sends Claude-shaped
`{hook_event_name,tool_name,tool_input}`, the Copilot CLI sends camelCase `{toolType:camelCase, toolCalls:[{name,args}]}`.

## 2. Bridge → dial (ESPHome native API action `buddy_line(line: string)`)

Each line is one Claude desktop Hardware Buddy JSON object
([REFERENCE.md](https://github.com/anthropics/claude-desktop-buddy/blob/main/REFERENCE.md)):

- **Heartbeat.** Sent on every change, coalesced over 150 ms, plus a 10 s keepalive. The dial counts 30 s of
  silence as offline.
  ```json
  {"total":2,"running":1,"waiting":1,
   "prompt":{"id":"p3-mwm6…","tool":"Bash","hint":"api: rm -rf /tmp/x"}}
  ```
  `prompt` is the oldest waiting permission. It is absent when nothing is waiting.
  `"work":[{"id":"s1","name":"auto-ai","tool":"Edit"},{"id":"s2","name":"auto-ai","tool":"Bash","agent":"codex"}]`
  lists every working session, ordered by session start (stable, so swiping on the dial doesn't shuffle). `name` is the
  session's `/rename` title, else its directory; `tool` is its current tool (empty at a prompt); `agent`, or `avatar`
  plus `avatar_color`, as below. `avatar` and `avatar_color` (0xRRGGBB) mark a Grok Bot session: its bot's shape (a pack
  scene `grok_<avatar>`, from `art/grok/`) and colour. The dial plays that scene with its body tinted, in place of
  cook or build. Any hook event that carries `avatar` and `avatar_color` gives its session that look, so `curl` can test it. Absent when nothing works. `"latest":"s1"` is the id of the one that did something
  last: the dial falls back to it when its focused session ends.
  A session's `"agent":"codex"` or `"agent":"copilot"` appears when it came through `hook.sh codex`
  or `hook.sh copilot` (installed in `~/.codex/hooks.json` and `~/.copilot/settings.json` by `bridge/install.mjs`).
  The dial plays `codex_work` or `copilot_work` in place of cook or build.
  `"bots":[["cloud",48294],["wedge",16750592]]` lists every distinct Grok bot look in the rosters (absent with no
  Grok Bot). The idle face takes turns by provider, switching every 15 s as the usage flash starts: Clawd's relax,
  Codex's pet (pack scene `codex_relax`), Copilot's head (`copilot_relax`), then the next Grok bot tinted.
  The bridge adds `"usage":"Claude 5h 17% · 7d 19% · 2h 33m","usage_pct":19`: one provider's plan utilisation
  plus time to its reset. The source is CodexBar's HTTP server (`GET http://127.0.0.1:6969/usage`, override with
  `CODEXBAR_URL`; loopback needs no token), polled every minute, one entry per enabled provider that returned
  usage. If CodexBar is down, the bridge falls back to Claude only, via `~/.claude/refresh-usage.sh` and
  `~/.claude/stats-usage.json`. The dial flashes the line on the lower arc for 3 s of every 15 s, amber at ≥80%
  and red at ≥95%. When the bridge sees that flash in `buddy_ui`, it switches to the next provider 4 s later, so
  each flash shows a different one.
- **Turn.** Sent when a session stops with assistant text; it shows on the done card.
  ```json
  {"evt":"turn","role":"assistant","content":[{"type":"text","text":"api: All tests pass."}]}
  ```
  `"sid":"<session id>"` names the session that wrote it. `"busy":2` appears when that many other sessions are still
  working: the dial then keeps its face on them and queues the reply as a notice (the bell pill, "2 new") instead of
  showing the card; tapping the bell opens the oldest. Without `busy` the card shows at once, as before.
  `"read":true` (instead of `busy`) marks a reply from a session the bridge just typed voice words into: you are in
  that conversation, so the card shows at once and, when it ages out, it is dropped instead of becoming a bell notice.
- **Seen.** Sent on every `UserPromptSubmit` (a prompt typed at the keyboard, or voice words typed in):
  `{"evt":"seen","sid":"<session id>"}`. That session's older replies are stale: the dial drops its bell notices, and
  its reply card if the card holds that session's reply.
  When the writer isn't Claude Code, the turn names it, using the heartbeat's keys. The card then shows that writer's
  name on the top arc:
  ```json
  {"evt":"turn", ..., "avatar":"cloud","avatar_color":48294,"avatar_name":"Ada"}
  {"evt":"turn", ..., "agent":"codex"}
  ```

## 3. Dial → bridge (ESPHome text sensors)

| Sensor | State |
|---|---|
| `buddy_decision` ("Buddy Decision") | `"<prompt id> once"` or `"<prompt id> deny"` after a tap on Allow / Deny. |
| `buddy_ui` ("Buddy UI") | The **ui** object below. Published whenever what is on the glass changes. Also logged as `[buddyui] state {…}`, so a log follower (`esphome logs`) can rebuild the screen without the API. |

**ui** (v1). Fields that are not on screen are left out.
```json
{"v":1, "view":"brand|face|permission|usage|battery|ota", "asleep":false,
 "name":"auto-ai ·2",                               // top arc: the focused session ("·2": the second with that name)
 "status":{"text":"Brewing… 0:12","color":"#00ff2f","shimmer":true},   // lower arc
 "scene":"claude_relax|claude_work|claude_build|claude_send",          // pet scene playing
 "alert":"2",                                       // alert bubble count on the working scene
 "card":"api: All tests pass.",                     // recap card (Dismiss button above it)
 "bell":"2 waiting",                                // bell pill: "N waiting", "N new" (replies from other sessions)
 "dots":"2/3",                                      // page dots (more than one session working): focused / working
 "word":"Claude",                                   // brand view
 "perm":{"title":"Allow Bash?","hint":"api: rm -rf /tmp/x","count":"1 of 2 waiting"},
 "voice":"listening|wait|confirm|send|note","sid":"s1",   // hold-to-talk (section 5): state, and the session it types into
 "alarm":"87% Grok usage"}                          // usage_pct >= 80 during the usage flash: red, pulsing, pet hidden,
                                                    // provider under it; 3 flashes per provider per percentage
```
Scenes and shimmer animate on the receiver's clock: the timing is fixed, so a mirror only needs the names.

## 4. Taps (ESPHome API action `buddy_tap(target: string)`)

Each target does exactly what the matching touch on the glass does (`buddy/ui.h` `tap()`):

| target | effect |
|---|---|
| `allow` / `deny` | Answer the permission screen. |
| `later` | Put the prompt off (swipe right / BOOT). The face shows the alert bubble or bell. |
| `card` | Dismiss the recap card. |
| `bell` | Reopen a put-off prompt; from the bell pill also open the oldest queued reply and focus its session. |
| `next` / `prev` | Swipe left / right on the face: focus the next / previous working session (wraps). |
| `back` | Close the usage or battery view. |
| `wake` | Wake the screen only. |
| `screen` | PWR: screen on/off. |

## 5. Voice (hold the face, speak, release)

Hold the face 650 ms (LVGL `LONG_PRESSED` on the face or the reply card). The dial picks the **target session** then and
keeps it in `sid`: the focused working session, an opened notice's session, or the session whose reply is on the card.
With none (idle, nothing on the card) it shows "No session to talk to" and does not listen.

```
dial   hold            ui {voice:"listening", sid}      voice_assistant starts (no VAD)
dial   ──VoiceAssistantRequest(start)──▶ bridge          bridge answers VoiceAssistantResponse(port 0): audio over the API
dial   ──VoiceAssistantAudio*──────────▶ bridge ──audio-chunk*──▶ Wyoming STT (127.0.0.1:10300)
bridge ──{"evt":"voice","text":"…"}────▶ dial            partials (STT servers that stream them), shown live on the reply card
dial   release          ui {voice:"wait"}                mic runs 300 ms more, then VoiceAssistantRequest(stop)
bridge ──{"evt":"voice","final":"…"}───▶ dial            sent only after audio-stop; or {"evt":"voice","error":"…"}
dial   ui {voice:"confirm"}                              final text on the card 2 s, "Tap to cancel"
dial   ui {voice:"send"} (or voice gone = cancelled)     bridge types text + Return into the session's iTerm tab
```

**Router.** Between audio-stop and `final`, the bridge decides where the words go (`bridge/router.mjs`): name rules first
("tell api to ...", "api, ...", bare allow/deny with one open prompt), else one JSON call (`{target, text}`, 3 s limit) to a
fast model through cli-proxy-api. The `final` line then carries what will happen on `send`: the text (focused session),
`name: text` (another session), `Assistant: text` (typed into the tmux session `voice`, a Claude that can act on the Mac),
`Allow|Deny name: Tool` (answers that session's open prompt via the held PermissionRequest), or a reply to speak
(said on `send`). With no sessions at all, the words go straight to the assistant. When the `voice` tmux session is not
running, a built-in assistant answers instead (`router.mjs` `chat`: one short spoken-style reply from the same model, no
tools, no memory): the reply is the `final` and is spoken on `send`. `error` "Assistant offline" only when that call fails. API error, junk or timeout: types into `sid`, or goes to the assistant when `sid` is
empty, with a log line.

**Ask.** When the words must go to a session and none is on the face and none is named, the bridge keeps the text, speaks
"Which session?" on the dial speaker (media_player announcement) and, only after playback has ended, sends
`{"evt":"voice","ask":"Which session?"}`. The dial (in `wait`) shows the question on the card and listens again with no
hold: `ui {voice:"listening"}` for 4 s (`VOICE_ASK_MS`) or until a tap, then the usual `wait`. The next `final` is matched
as a session name (exact, ignoring case and punctuation, unique): a hit sends the original text there through the normal
`confirm` card (`name: text`); a miss is `error` "No session called <answer>" and nothing is sent. The question is dropped
on any outcome and by a new hold.

- Lines carry one of `text` (partial), `final` (the transcript; empty = nothing heard) or `error` (shown for 2 s).
- The bridge only types text it produced itself for that dial (`final`), into the terminal of `sid`
  (`Board.tty(sid)`, from `hook.sh`'s `tty`, matched to an iTerm session's `tty` by AppleScript). No tty for the session
  (Claude Code in VS Code, say): the words go to the assistant instead; nothing is typed. "Which session?" is only asked
  when some session has a tty. A `send` with no pending text is ignored.
- Text goes in with `write text … newline NO`, then a separate bare newline (Claude Code reads text + Return in one
  burst as a paste). It travels as an `osascript` argument, never inside the script.
- ESPHome's voice assistant takes **one** API client. The bridge subscribes (`SubscribeVoiceAssistantRequest`); if Home
  Assistant has the dial and subscribed first, the bridge is refused. `@2colors/esphome-native-api` has the protobuf
  classes but not the voice message ids: `bridge/voice.mjs` registers 89, 90, 91, 92 and 106 in its id table at runtime.
- Audio is 16 kHz, 16-bit, mono (ES7210 → I2S, full duplex with the speaker, so sounds play at 16 kHz too).
- STT: `VOICE_STT_HOST` / `VOICE_STT_PORT` (127.0.0.1:10300). The Swift `AppleSTT` server streams `transcript-chunk`
  partials. `wyoming-macos-stt` (the Python one under launchd now) sends only the final `transcript`: the card shows
  "…" while you speak.

**Speaking (bridge → dial speaker).** Not a `buddy_line`: the bridge sends the ESPHome `MediaPlayerCommandRequest`
(`media_url`, `announcement: true`) to the dial's media_player. Text → claude-speak rules → Wyoming `synthesize` on
127.0.0.1:10200 (`audio-start`/`audio-chunk`*/`audio-stop`, 16 kHz s16 mono) → WAV at `http://<bridge ip>:19877/tts/<id>.wav`
(in memory, 60 s). The dial's announcement pipeline is WAV; its `on_announcement` / `on_idle` switch the amp. Done =
media_player state `ANNOUNCING` then anything else. `POST 127.0.0.1:19876/speak {"text"}` triggers it by hand and answers
`{"played":bool}` when playback ends. A router `speak` reply is said when the dial sends `send` (after the 2 s confirm).

## 6. Simulator

There is no separate protocol: `claude-buddy-sim.yaml` runs the dial's own firmware UI on the Mac (ESPHome `host`,
SDL window) and the bridge talks to it exactly as to the dial, as one more `DIAL_HOST`.
