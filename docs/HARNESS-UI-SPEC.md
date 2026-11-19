# Harness dial UI spec (Claude parts), for an ESPHome + LVGL port

Source: the Harness device firmware at `vendor/openharness/devices/harness-device/firmware/main/`.
In this doc, a bare filename like `ui_habitat.c` means `main/ui/habitat/`. `cable_client.c`, `ptt.c` and
`app_main.c` live in `main/`. Pet art and loops were already decoded to `docs/harness-pets/` (`data.js` and PNGs).
Facts marked *(guess)* or written as a question have not been verified. Everything else was read in the source.

---

## 1. Daily use

**Boot.** The screen shows only the wordmark "Harness" (Inter Bold 48, centred, line at y 208) until the host
daemon's `welcome` arrives and the agent list loads (`render_brand` ui_habitat.c:901-906, used when
`!s.connected || s.loading` at :1116, and boot parks there via `ui_enter_boot_loading` app_main.c:216). The
firmware never draws a "Not connected" screen. That phrase only appears in comments (cable_client.h:9,
app_main.c:231). Holding BOOT through power-on does a factory reset (app_main.c:188).

**Idle.** One screen, the *Focus face*, always shows one agent (a "pane"). The agent's name curves along the
top edge. For Claude, the middle is a **relaxing scene** (Clawd juggling three balls), with a short grey hint
on the lower arc: "Tap to talk", "Hold for tabs", "Tap name for panes", "What's next?", "Your move",
"Say the word" or "Ask away" (focus.c:91-102). A hint is picked at random and never repeats twice in a row
(focus.c:117-134).

**Agent running.** The middle becomes the **working scene**: either Clawd cooking (`claude_work`) or Clawd building
(`claude_build`), chosen 50/50 when the scene first shows for that agent (focus.c:219-231). The lower arc shows
the live line in voice green: "<tool or verb>… 1:05" (focus.c:764-791, :1022-1026). With no tool text, the verb
"Working" cycles every 6 s through Brewing, Cooking, Churning, Frosting, Simmering, Tinkering, Conjuring, Composing,
Percolating, Wrangling, Hatching, Concocting and Puttering (focus.c:753-762). The timer counts from when the dial
heard `turn.started` (ui_habitat.c:1150-1158). A turn that has heard nothing for 25 s is treated as over
(`BUSY_FUSE_MS` ui_habitat.c:71).

**Needs an answer.** The question's prompt text replaces the recap, in the recap card on the face. On the face
it is **read-only**: "the answer is given in the app, so the face grows no buttons and no other screen opens for
it" (ui_habitat.c:1118-1125). The small Claude pet sits above the card. The display wakes if that agent was not
the one on the glass (ui_question_show :4077-4099). The full answer flow (QUESTION → CHOICE → ANSWER_REVIEW, §3)
opens only through the "visit" feature, from `ui_focus_project` :3769 and `ui_visit_state` :4602. Grep found no
code that ever dispatches `A_QUESTION`. Tapping an inbox notice opens the agent on the desktop
(`A_NOTICE` → `A_DESKTOP`, :2355-2385).

**Finished.** `turn.done` stops the busy state. The `summary` that follows carries the recap
(cable_client.c:1281-1317). On the face's own agent, the recap shows in a card (Inter 30, up to 4 lines) with
the small pet above it, and the notice is marked read at once. For any other agent, the dial files a notice,
beeps (at most once per 30 s), wakes the display, and, if the tier is `alert`, sets a 2 s "celebration" pose
(at most once per 20 s) (ui_habitat.c:3925-3950). That pose has **no visible effect** on the Claude face: all four
small-pet loops are identical (pets.c:18, `data.js` `loops`), and DONE vs IDLE both pick the relaxing scene
(focus.c:245-248).

**Multiple agents.** The face shows one agent. Other agents' unread notices show as a blue bell pill with a
count at y 400 (ui_habitat.c:1098-1112). On the working face, the pill is replaced by the pet's **alert bubble**
next to the hat (focus.c:252-270). `notice_unread` leaves out the agent on the face (ui_habitat.c:360-366).
- Swipe left/right: next or previous agent.
- Tap the name: the pane picker carousel.
- Hold the face: tabs or apps.
- Pull down: the inbox.

**Voice push-to-talk.** A short touch anywhere below the name opens the mic on the face's agent (§3). The
listening scene (Clawd in headphones, nodding to the mic level) plays with "Listening" sweeping along the
lower arc. A tap anywhere stops recording, and the sending scene (Clawd posting a letter) plays until the
host answers.

**Drafts / carry / selection / forms / visit.** These are desktop-coupled features behind cable feature flags:
- Select text from the terminal (`selection.c`), then carry it to another agent (`carry.c`).
- Review or edit a spoken draft before sending (`draft.c`; "Your draft", "Add to message", "Undo last edit").
- Voice-driven "Find Harness" / "New Harness" forms (`form.c`).
- Jump to an agent's window and back (`visit.c`, "[ return ]").

None of them apply to the Buddy protocol.

**Sleep / dim.** The panel sleeps after 300 s with no activity (display_habitat.c:405-411). A touch while asleep
only wakes it and swallows the contact (touch_habitat.c:133-138). A PWR tap toggles the screen. Holding BOOT for
800 ms also toggles it (ptt.c:15-24). "Nap" (15 min, holds the pet still) exists in the actions list
(ui_habitat.c:2574-2577), but grep found no path that opens the SETTINGS list. Brightness is set from the host,
with an 8 % floor (display_habitat.c:234-238).

### All views (`view_t`, ui_habitat.c:72-94)
HOME/AGENT (the same Focus face, :1353-1356) · AGENTS (pane picker) · READER · QUESTION · CHOICE · ANSWER_REVIEW ·
INBOX · TABS · APPS · MACHINES · VOICE · SELECTION · FORM · DRAFT · DRAFT_OPTIONS · SETTINGS · STOP · MODELS ·
MESSAGE (title + text, turn errors auto-close after 8 s) · OTA (wordmark only).

Focus-face sub-states (each is the same view, laid out differently): brand/connecting · relaxing · working
(cook/build) · working + alert bubble · recap card · question card · retry/hint line · voice listening ·
voice sending.

---

## 2. Screen-by-screen spec (Claude flow)

### Common
- Panel 466×466. The canvas is pure black `0x000000` (`HT_THEME_FOCUS_CANVAS`, theme.h). All ink stays inside r 230.
- **Theme** (theme.h): text `0xefe7de`, secondary `0xada6ad`, accent `0xc6aaef` (lavender; `0xff6d00` only with
  the `DEVICE_HABITAT_ORANGE` build flag, main/CMakeLists.txt:97), selection `0x392c4a`, error `0xe7a6ad`,
  voice green `0x00ff2f`, done `0x0dbc79`, question `0xe5e510`, failed `0xcd3131`.
- **Focus-only inks** (focus.c:57-64): name `0xeaeaf0`, recap text `0xd6d6d2`, card fill `0x1c1e26` with rim
  `0x3d3f47` (radius 24), resting text `0x585863`, relaxing arc line `0x9a9aa6`, live line `0x00ff2f`.
  Bell/alert blue `0x006fff`.
- **Fonts:** every word is Inter, from `lv_font_conv` 4 bpp, generated into focus_faces.c. Sizes used: 20, 25,
  Medium 26 (arcs), 30, 36, 44, Bold 48. `fonts.c` holds generated mono bitmap fonts. The legacy "mono 28" UI
  font is drawn as Inter 25 (ui_habitat.c:753-756).
- **Arcs** (terminal.h:158, :231): the name runs on the top arc, cells from y 12, 32 px tall, a 390 px span
  (26×15). The status runs on the matching lower arc. Both use Inter Medium 26. Overlong text ends in "…".
- **Transitions:** `view()` is an instant swap with no fade (ui_habitat.c:566-586). The only easing is the
  carousel settle: quadratic ease-out over 192 ms (`HT_TAB_SETTLE_MS`, workspace.c:93-105; workspace.h:32). Pets
  animate on wall clock. Nothing else on the face moves (focus.c:1043-1047).
- **Pet clock:** frozen on step 0 when Quiet is set, the display is asleep, the dial is offline, or Nap is on
  (focus.c:188-191, ui_habitat.c:1176). Pets do not advance while a finger is down (ui_habitat.c:1031-1033).

### 2.1 Connecting / loading
Only "Harness" in `0xefe7de`, Inter Bold 48, centred on a 50 px line whose top is y 208 (ui_habitat.c:901-906).
The pet and text are hidden. → **Replace with the BLE pairing flow (§5).**

### 2.2 Relaxing (idle, nothing to show)
Applies when there is no recap, no activity and no status, and the agent is idle or done (focus.c:240-249).
- Top arc: the agent's name (`0xeaeaf0`). A tap on the name area `{83,0,300,66}` opens the pane picker
  (ui_habitat.c:1199-1202).
- Centre: `claude_relax`, 120×102, centred, 4 px lower plus scene dy 14 (focus.c:374-378, pets.c:222).
  4 frames, a 24-step loop at 55 ms (1.32 s), with a per-step bob of 0/-1 px (`claude_relax_dy`).
  6 code-drawn ring shapes (3 juggling balls) per step (`claude_relax_shapes`, pets.c:219-221).
- Lower arc: one RELAXING hint in `0x9a9aa6`. It is left out while the bell pill shows (focus.c:1027-1031).
- If the pet has no relax scene, the fallback is the small pet plus a resting line in Inter 36 `0x585863`, up to
  2 lines, 276 px wide (focus.c:1016). Those lines are "Let's build it", "Do anything", "What's next?",
  "Ready when you are", "Say the word", "Make it happen", "Start something", "Tap to talk", "Hold to switch tabs"
  and "Tap the name to switch panes" (focus.c:80-89).

### 2.3 Working
Applies when there is no recap and an activity line is set (focus.c:181-187, :212-218).
- Centre: `claude_work` or `claude_build` (pets.c:66, :188). Origin is centred, then +4 px, then dx 1 / dy -4.
  - `claude_work`: 117×117, 2 frames, 26 steps at 55 ms (1.43 s), hop `claude_work_dy` 0/-1. The overlay
    `claude_work_ov` (17 frames, steam/props) is placed per step by `claude_work_ov_at`.
  - `claude_build`: 251×141, 32 steps at 55 ms (1.76 s), with overlay `claude_build_ov`.
- Lower arc: "<verb>… <elapsed>" in `0x00ff2f`. The verb is cut a letter at a time so the time always fits
  (focus.c:777-791). The time format is "0:34", "1:05", "1h 5m", "2d 3h" (focus.c:742). If a footer control
  owns the bottom edge, the line moves to a straight Inter 30 line at y 334 (focus.c:997-1004).
- **Alert bubble** (`claude_alert`, pets.c:207): plays while other agents have unread notices.
  - 46 steps at 110 ms (5.06 s): pop-in over steps 0-5, a ringing bell, then the last step is held until the
    notices are read (focus.c:259-270).
  - The count appears from step 6, in Inter Medium 26 in `0x006fff`, or "9+" in Inter 20 (focus.c:963-990).
  - Tapping the bubble (14 px margin) opens the inbox (ui_habitat.c:1206).

### 2.4 Recap (done) and question card
These share one layout. The question prompt *is* the recap text (ui_habitat.c:1125).
- Card: x 41, w 384, radius 24, fill `0x1c1e26`, rim `0x3d3f47`. Its height is lines×43 + 2×21 padding
  (focus.c:879-885, :952).
- Text: Inter 30 in `0xd6d6d2`, a 364 px column at x 51, each line centred, 43 px pitch, at most 4 lines. The
  recap is cut to 90 codepoints and then to fit, ending in "…" (focus.c:793-822). The block is centred
  vertically in y 176-376 (focus.c:49-56).
- Pet: the small Claude (73×50 at 1x; cells drawn at 2x and zoomed). It is 1.5x when the text is ≤3 lines and
  1x at 4 lines (focus.c:886-889). It is centred between the foot of the name and the top of the card, and
  stays put (focus.c:925-944).
- Small loop: 9 frames, 24 steps at 120 ms (2.88 s), **the same for idle, working, done and asking**
  (pets.c:18-19). Exports: `docs/harness-pets/assets/claude_frames`, zooms `assets/claude/z4..z8`.
- Fallback text when a question has no prompt: "Needs your answer" (ui_habitat.c:706, :4086).
- A recap stays until the next `turn.started` clears it (`recap_ready=false`, ui_habitat.c:3663).

### 2.5 Retry / hint line
After a failed voice turn the face shows "Try again". If the agent can't take voice, it shows
"See it in <host app>" for 2 s instead. Either one is a single Inter 30 line in `0xeaeaf0`, centred, with the
pet at 1.5x (ui_habitat.c:1149, :3051-3059; focus.c:877-878).

### 2.6 Bell pill (non-working faces)
Applies when there are unread notices from other agents.
- Pill: `0x006fff`, 32 px tall, fully rounded, 13 px side padding.
- Content: a FontAwesome bell (Montserrat 14) and, 6 px later, the count in Inter 20 `0xeaeaf0`.
- Placement: centred at y 400. Hit area `{83, 382, 300, 84}` (ui_habitat.c:1098-1112, :1203-1204).

### 2.7 Voice (DROP for Buddy, but the art is reusable)
- **Listening:** `claude_listen`, 161×123, 1 frame, 56 steps at 60 ms (3.36 s) × 5 mic levels. The nod comes
  from `claude_listen_dy`. Waves are 3 arcs per side in rgb(252,206,92), period 1680 ms (pets.c:71-75).
- "Listening" sweeps along the lower arc: a 2-letter bright band crosses in 900 ms, then rests 400 ms (1300 ms
  cycle, 65 ms steps, 30 % → 100 % brightness) (focus.c:443-463).
- **Sending:** `claude_send`, 192×109, 40 steps at 60 ms (2.4 s), dy 14, with overlay `claude_send_ov` (the letter)
  (pets.c:134).
- Pets without scenes get 7 green bars on a 22.4 px pitch (1.2 s cycle) or three sparkles 66 px apart
  (focus.c:569-697).

### 2.8 Answer screens (visit-only today; reuse the layout for the permission screen)
Common heading: "<" back control at (87,67), title at x 147. Body text is Inter 25 at y 146, 348 px wide,
3 rows per page. The position line uses Inter 20 at y 276 (ui_habitat.c:950-954, :1364-1380).

| View | Strings | Controls (x, y, w) |
|---|---|---|
| QUESTION loading | "Reading the question..." (y 214) | "[ later ]" (122,346,187) |
| QUESTION error | "Answered elsewhere." / "This question needs the desktop." / error text | "[ on desktop ]" (116,346,238) |
| QUESTION | "question 1 / 2" (y 109), prompt, "1-3 / 7  drag to read" | "[ later ]" (79,346,153), "[choices]" (236,346,153); with voice: "[later]" "[say]"/"[draft]" "[next]" at y 336 |
| CHOICE | "choose one 2 / 4" / "choose any …", option text, "drag for choices" | "[ select ]"/"[ selected ]" (FACE_CX 204, y 300), "[back]" (88,366), "[review]" (258,366) |
| ANSWER_REVIEW | "answer 1 / 1", the chosen text; "Waiting..." while sending | "[back]", "[again]" (if spoken), "[next]"/"[send]" |
| STOP | "stop this turn?", "Your work stays." (y 185), "This turn stops." (y 227, DIM) | "[cancel]" (83,355,156), "[stop]" (263,355,120) in error ink |

(ui_habitat.c:1381-1458, :2042-2047.) Controls are Inter 25 in `0xefe7de`. Pressed controls get accent ink on a
`0x392c4a` background. Disabled controls use `0xada6ad`. Hit boxes are the label plus a margin, 66 px tall
(:907-922).

### 2.9 State → animation
Logic: focus.c:181-249, :476-487, ui_habitat.c:968-982.

| Agent state | Scene / loop | Lower arc / centre text |
|---|---|---|
| offline / loading | none (wordmark) | "Harness" |
| idle, or done with recap dismissed | `claude_relax` + balls | random RELAXING hint, grey |
| working | `claude_work` or `claude_build` (random per agent) | "Verb… m:ss", green |
| working + unread from others | the above + `claude_alert` bubble overlay with count | same |
| done, recap | small `claude_frames` loop, 1.5x/1x, above card | recap card |
| asking | small loop above card | question card |
| voice recording | `claude_listen` (level-driven) | "Listening" sweep |
| voice sending | `claude_send` | none |
| asleep / quiet / nap | frame held at step 0 | n/a |

---

## 3. Interaction model

### Touch classification
gestures.c:4-45, ui_habitat.c:2857-3157.
- **Move:** ≥12 px from the start point; the axis is locked by the larger of |dx| and |dy|.
- **Tap:** not moved, 25-350 ms. **Hold:** not moved, 650-1800 ms. Anything else is NONE.
- **Guard:** after voice actions, new contacts are ignored for 450 ms.
- **Face tap:** relaxed rules. Any contact on `{0,66,466,400}` that stayed within 24 px and lifted before 650 ms
  counts as a tap and starts voice (ui_habitat.c:3041-3066).
- **Face hold:** fires *while still held* at 650 ms (no release needed) and opens tabs or apps
  (ui_habitat.c:1040-1046, `hold_open` :637-641).
- **Footer button:** counts if the release lands back inside the pressed rect within 1800 ms (:3018-3040).
- **Vertical swipe** (|dy|>55): on the face, pulling down with unread notices opens INBOX (:3104-3110). On
  other views, a swipe up starting at y ≥400 goes HOME (:3113). Lists page by 3.
- **Horizontal swipe** (|dx|>60): on the face, next/prev agent (:3145-3150). On question views, swiping right
  goes back one level (ANSWER_REVIEW → CHOICE → QUESTION → HOME) (:3141-3142). On other views, back to HOME.
- **Scrolling in question views:** a vertical drag scrolls the text one row per 40 px. On CHOICE, dragging past
  the end of an option moves to the next or previous option (`question_move` :2219-2236). That is what
  "drag for choices" means.
- **Carousels** (tabs, panes, inbox, apps): 320 px pages and 2× drag gain. Pages snap with momentum
  (±half a page), and a tap on the centred page opens it (workspace.c:107-160).
- **Wake touch:** a touch while asleep only wakes the screen (touch_habitat.c:133-138).

### Keys (ptt.c)
- **BOOT tap** (on press): wakes the screen if needed, then runs `ui_boot_pressed` (ui_habitat.c:4716-4727). It
  aborts voice if recording. On the AGENT view with a busy agent, it opens the STOP confirm. Otherwise it goes
  HOME. Note: on HOME a busy agent is *not* stopped; only AGENT does that (AGENT is set after a swipe or pick).
- **BOOT hold 800 ms:** toggles the screen (ptt.c:25, :61-64).
- **PWR tap:** toggles the screen, read over I²C from the AXP2101. PWR long press is a hardware restart
  (ptt.c:20-22).
- **BOOT held at power-on:** factory reset.

### Question / answer flow
Shown on the face (read-only). When opened through a visit:
1. QUESTION shows the prompt. "[choices]" or a drag leads to CHOICE.
2. CHOICE: drag between options, tap "[ select ]" (single-select replaces, multi-select toggles), then
   "[review]".
3. ANSWER_REVIEW: "[send]" queues the answer and shows "Waiting...".
4. Up to 4 questions per request, 6 options each (`QUESTION_MAX`/`OPTION_MAX` :42-43). "[next]" walks the
   questions (`send_answer` :2237-2245).

Deadlines and errors:
- The question must load within 4 s, or "Question did not arrive. Open the alert again." appears.
- The answer receipt must arrive within 15 s, or "No answer receipt. Check the terminal before trying again."
  appears (:2198, :2243, :3226-3229).
- If the question changes mid-read: "The question changed. Open the alert again."
- If it was answered on the desktop: "Answered elsewhere."

### Timings
| What | Value | Where |
|---|---|---|
| tap / hold / move | 25-350 ms / 650-1800 ms / 12 px | gestures.c:28, :41-43 |
| face tap tolerance | 24 px, <650 ms | ui_habitat.c:3041 |
| gesture guard | 450 ms | gestures.c:11 |
| carousel settle | 192 ms quad ease-out | workspace.h:32 |
| boop pose | 900 ms (no visual on Focus) | ui_habitat.c:2541-2546 |
| celebration | 2 s, ≥20 s apart | ui_habitat.c:3941-3945 |
| busy fuse | 25 s | ui_habitat.c:71 |
| notice TTL | 6 h | ui_habitat.c:3902 |
| error MESSAGE auto-close | 8 s | ui_habitat.c:3903 |
| hint ("See it in …") | 2 s | ui_habitat.c:3058 |
| screen auto-sleep | 300 s idle | display_habitat.c:405 |
| nap | 15 min | ui_habitat.c:2576 |
| voice: max record / reply wait | 600 s / 65 s | ui_habitat.c:3262, :2528 |
| BOOT long press | 800 ms | ptt.c:25 |
| host silence → disconnected | 15 s | cable_client.c:42, :1455 |
| recap/arc gerund rotation | 6 s | focus.c:760 |
| pet loops | relax 24×55, work 26×55, build 32×55, alert 46×110, listen 56×60, send 40×60, small 24×120 ms | pets.c |

---

## 4. Mapping to the Hardware Buddy BLE protocol

The Buddy protocol is aggregate. A heartbeat describes *all* sessions at once (`total`/`running`/`waiting`),
not one agent, so the Harness's per-agent face collapses to a single "Claude" face.

| Harness state / screen | Buddy signal | Notes |
|---|---|---|
| Connecting wordmark | BLE not bonded/connected | Replaced by pairing (§5). |
| Disconnected (`ui_set_connected(false)`) | no heartbeat for N s | Harness uses 15 s silence. Same threshold? *(guess)* |
| Name on top arc | `name`/`owner` from the host's name/owner message | Or a fixed "Claude". |
| Relaxing scene + hint | `running==0 && waiting==0`, no fresh turn text | The hint lines that teach tap/hold no longer apply. Rewrite them, e.g. "All quiet". *(guess)* |
| Working scene (cook/build) | `running>0` | Pick cook/build at random on each 0→>0 edge, as the original does. |
| Live line "Verb… m:ss" | verb: latest `evt:"turn"` `tool_use.name`, else `msg`, else rotating gerund; time: local stopwatch from the `running` 0→>0 edge | `entries[0]` ("10:42 git push") could stand in for `tool` once the time is stripped. *(guess)* |
| Alert bubble with count | `waiting>0` while `running>0` | The count is `waiting`. Harness counts other agents' unread notices, so this is a semantic shift. |
| Bell pill with count | `waiting>0` when not working | Same shift. |
| Question card ("Needs your answer") | `prompt` present | Card text: `prompt.tool` + `prompt.hint` (e.g. "Bash: rm -rf /tmp/foo"). Fallback string "Needs your answer". |
| Answer flow (CHOICE/ANSWER_REVIEW) | `prompt.id` → send `{"cmd":"permission","id":…,"decision":"once"\|"deny"}` | **NEW screen.** Harness answers on the desktop. Reuse the CHOICE layout (two options, "Allow once"/"Deny") or the STOP 2-button layout. Clear on `prompt` absent or `id` changed (mirrors "The question changed…"). Use the 15 s receipt timeout → the status ack? |
| Recap card (done) | `evt:"turn"` text blocks, shown once `running` drops to 0 | Cut to 90 chars / 4 lines as the original does. Ignore `thinking` blocks. |
| Celebration / beep / wake on done | `running` >0 → 0 with new turn text | Beep and wake (the pose is invisible anyway). |
| Turn error MESSAGE | none | DROP. |
| Elapsed counter | local timer only | The protocol has no start time. |
| Inbox (notices list) | `entries[]` (≤5 strings) | Closest match. Could be a carousel of entries. *(guess)* Otherwise DROP. |
| Pane picker / swipe agents / tabs / apps / machines | none (`total` is a count only) | **DROP.** Maybe show "3 sessions" somewhere. *(guess)* |
| Voice PTT, listening/sending scenes | none | **DROP** (art could be reused for other moments). |
| Drafts, carry, selection, forms, visit, Latest output, READER | none | **DROP.** |
| STOP turn | no cancel command | **DROP.** BOOT tap needs a new role: deny the open prompt? (open question) |
| Model picker, Nap, settings, OTA, pet packs | none | **DROP** (OTA and Nap become ESPHome concerns). |
| (no Harness equivalent) | `tokens`, `tokens_today` | Harness never renders tokens (`agent_t.tokens` is unused). Optional new stat on the lower arc. |
| (no Harness equivalent) | time sync | The Harness has no clock ("the cable carries no time", ui_habitat.c:1672). Optional idle clock. |

**Gesture repurposing (open questions for Rob):** face tap (was voice), face hold (was tabs), tap on the name
(was panes) and swipe (was next agent) all lose their targets. Candidates: tap = boop/wake, hold = show entries
or token stats, BOOT tap = deny the prompt or go home.

---

## 5. Boot / connection screens to replace

What exists today:
- Power-on → `ui_enter_boot_loading()` → the wordmark (app_main.c:216). If the USB link fails to start, it
  retries once, then keeps showing the wordmark with no message (app_main.c:221-233).
- `welcome` → `session_up` → `ui_set_connected(true)` (cable_client.c:888). The face appears once the agent list
  lands (`s.loading` cleared).
- 15 s of silence → `session_down` → `ui_set_connected(false)` (cable_client.c:910-946, :1455). This aborts
  voice, closes question/form/draft views, cancels visit and carry, and falls back to the wordmark.
- OTA uses the same wordmark (ui_habitat.c:2063-2065).
- There is no pairing UI. The firmware states "nothing to pair: plugging the dial into a computer… IS the
  authorization" (app_main.c:6-9; README).

Proposed BLE replacement, styled to match:
1. **Unpaired / advertising:** wordmark "Claude" in Inter Bold 48 at y 208. Under it, "Claude-XXXX" in Inter 25
   `0xada6ad`. Optionally the relaxing scene above it.
2. **Passkey:** during bonding, show the 6 digits large (Inter 44, accent `0xc6aaef`) with
   "Enter on your computer" (Inter 25, secondary). Time out back to step 1.
3. **Bonded, not connected:** the wordmark, plus a lower-arc line in `0x9a9aa6` such as "Waiting for Claude"
   *(string is a guess)*. The pet is frozen, as the original freezes it when offline.
4. **Connected:** the first heartbeat goes to the relaxing face. A heartbeat gap of 15 s (or similar) returns to
   step 3 and drops any open prompt screen, mirroring `ui_set_connected(false)`.
5. **Factory reset:** keep BOOT-held-at-boot (app_main.c:188) to clear bonds.
