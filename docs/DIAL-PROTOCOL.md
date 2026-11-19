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
  The bridge adds `"usage":"Claude 5h 17% · 7d 19% · 2h 33m","usage_pct":19`: one provider's plan utilisation
  plus time to its reset. The source is CodexBar's HTTP server (`GET http://127.0.0.1:6969/usage`, override with
  `CODEXBAR_URL`; loopback needs no token), polled every minute, one entry per enabled provider that returned
  usage. If CodexBar is down, the bridge falls back to Claude only, via `~/.claude/refresh-usage.sh` and
  `~/.claude/stats-usage.json`. The dial flashes the line on the lower arc for 3 s of every 15 s, amber at ≥80%
  and red at ≥95%. When the bridge sees that flash in `buddy_ui`, it switches to the next provider 4 s later, so
  each flash shows a different one. The line also shows on the entries view.
- **Turn.** Sent when a session stops with assistant text; it shows on the done card.
  ```json
  {"evt":"turn","role":"assistant","content":[{"type":"text","text":"api: All tests pass."}]}
  ```

## 3. Dial → bridge (ESPHome text sensors)

| Sensor | State |
|---|---|
| `buddy_decision` ("Buddy Decision") | `"<prompt id> once"` or `"<prompt id> deny"` after a tap on `[ allow ]` / `[ deny ]`. |
| `buddy_ui` ("Buddy UI") | The **ui** object below. Published whenever what is on the glass changes. Also logged as `[buddyui] state {…}`, so a log follower (`esphome logs`) can rebuild the screen without the API. |

**ui** (v1). Fields that are not on screen are left out.
```json
{"v":1, "view":"brand|face|permission|entries", "asleep":false,
 "name":"Claude",                                   // top arc
 "status":{"text":"Brewing… 0:12","color":"#00ff2f","shimmer":true},   // lower arc
 "scene":"claude_relax|claude_work|claude_build|claude_send",          // pet scene playing
 "alert":"2",                                       // alert bubble count on the working scene
 "card":"api: All tests pass.", "small":6,          // recap card + small pet zoom (6 = 1.5x, 4 = 1x)
 "bell":"2 waiting",                                // bell pill
 "word":"Claude", "sub":"Waiting for the bridge",   // brand view
 "perm":{"title":"Allow Bash?","hint":"api: rm -rf /tmp/x","count":"1 of 2 waiting"},
 "list":"07:42 api: Bash npm test\n…",              // entries view (hold the face)
 "alarm":"87%"}                                     // usage_pct >= 80 during the usage flash: red, pulsing, pet hidden
```
Scenes and shimmer animate on the receiver's clock: the timing is fixed, so a mirror only needs the names.

## 4. Taps (ESPHome API action `buddy_tap(target: string)`)

Each target does exactly what the matching touch on the glass does (`buddy/ui.h` `tap()`):

| target | effect |
|---|---|
| `allow` / `deny` | Answer the permission screen. |
| `later` | Put the prompt off (swipe right / BOOT). The face shows the alert bubble or bell. |
| `bell` | Reopen a put-off prompt. |
| `card` | Dismiss the recap card. |
| `hold` | Open the entries view (hold the face for 650 ms). |
| `back` | Close the entries view. |
| `wake` | Wake the screen only. |
| `screen` | PWR: screen on/off. |

## 5. Simulator

There is no separate protocol: `claude-buddy-sim.yaml` runs the dial's own firmware UI on the Mac (ESPHome `host`,
SDL window) and the bridge talks to it exactly as to the dial, as one more `DIAL_HOST`.
