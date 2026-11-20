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

## 2. Bridge → dial (ESPHome native API action `buddy_line(line: string)`)

Each line is one Claude desktop Hardware Buddy JSON object
([REFERENCE.md](https://github.com/anthropics/claude-desktop-buddy/blob/main/REFERENCE.md)):

- **Heartbeat.** Sent on every change, coalesced over 150 ms, plus a 10 s keepalive. The dial counts 30 s of
  silence as offline.
  ```json
  {"total":2,"running":1,"waiting":1,"msg":"approve: Bash",
   "entries":["07:42 api: Bash npm test","07:41 api: prompt"],
   "prompt":{"id":"p3-mwm6…","tool":"Bash","hint":"api: rm -rf /tmp/x"}}
  ```
  `prompt` is the oldest waiting permission. It is absent when nothing is waiting.
  `"avatar":"cloud","avatar_color":48294` appear when the session shown as working is a Grok Bot one: its bot's
  shape (a pack scene `grok_<avatar>`, from `art/grok/`) and colour (0xRRGGBB, from the roster). The dial plays
  that scene with its body tinted, in place of cook or build. Any hook event that carries `avatar` and
  `avatar_color` gives its session that look, so `curl` can test it.
  `"work":[{"id":"s1","name":"auto-ai","tool":"Edit"},{"id":"s2","name":"auto-ai","tool":"Bash","agent":"codex"}]`
  lists every working session, ordered by session start (stable, so swiping on the dial doesn't shuffle). `name` is the
  session's `/rename` title, else its directory; `tool` is its current tool (empty at a prompt); `agent`, or `avatar`
  plus `avatar_color`, as below. Absent when nothing works. `"latest":"s1"` is the id of the one that did something
  last: the dial falls back to it when its focused session ends. The flat fields (`msg`, `agent`, `avatar`, …) still
  describe that latest session, so firmware that ignores `work` behaves as before.
  `"agent":"codex"` or `"agent":"copilot"` appears when the session shown as working came through `hook.sh codex`
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
 "word":"Claude", "sub":"Waiting for the bridge",   // brand view
 "perm":{"title":"Allow Bash?","hint":"api: rm -rf /tmp/x","count":"1 of 2 waiting"},
 "alarm":"87%"}                                     // usage_pct >= 80 during the usage flash: red, pulsing, pet hidden
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

## 5. Simulator

There is no separate protocol: `claude-buddy-sim.yaml` runs the dial's own firmware UI on the Mac (ESPHome `host`,
SDL window) and the bridge talks to it exactly as to the dial, as one more `DIAL_HOST`.
