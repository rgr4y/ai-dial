---
name: onboard
description: Map of the dial stack (hook.sh -> bridge -> ESPHome API evt lines -> claude_buddy component -> buddy/ui*.h). Load before changing or diagnosing anything about notices, the bell, the reply card, voice, sessions/focus, bridge events, or the dial UI, instead of re-grepping the repo to orient.
---

# Dial stack map

Pointers, not facts. If a symbol named here is gone, grep for it, fix this file, then carry on.
Rules (commits, sim-first, visible builds, YAML extend-don't-fork, beads) live in AGENTS.md and memory; not repeated here.

## Data flow, one hop per line

1. **Agent hooks** — `bridge/hook.sh` (registered by `bridge/install.mjs` in Claude Code / Codex / Copilot settings; peon-ping adapters via `bridge/peon-shim/`). POSTs hook JSON to the daemon. `PermissionRequest` blocks until the dial answers. Copilot has no permission hook, so `hook.sh copilot-gate` gates `PreToolUse` instead (mutating tools only; payloads normalised).
2. **Bridge daemon** — `bridge/dial-bridge.mjs` (LaunchAgent `com.rob.dial-bridge.plist`; restart freely). Folds events through `Board.event()` in `bridge/board.mjs` (pure, tested), returns `{cleared, turn, speaker, sid, busy}`.
3. **To the dial** — `sendLine()` in `dial-bridge.mjs` calls the ESPHome API action `buddy_line` (declared in `buddy-ui.yaml`) with one JSON line. Heartbeat snapshot debounced 150 ms + keepalive; event lines: `turn`, `seen`, `voice`. Protocol: `docs/DIAL-PROTOCOL.md`.
4. **Dial component** — `components/claude_buddy/claude_buddy.cpp` `feed_line()`: heartbeat goes to `snap_`; `evt` handlers by `strcmp(evt, ...)`. Owns `notices_` (bell queue, cap 9), `last_text_` (reply card), voice line, permission decide (`decision_sensor` text sensor `buddy_decision` back to bridge).
5. **UI** — `buddyui::update(now)` runs on `claude_buddy.on_update`. `buddy/ui.h` = overview, `tap()` router, widget build, `publish_state()` (mirror of the glass, published on `buddy_ui` text sensor). Views: `View` enum in `buddy/ui_state.h` (BRAND, FACE, PERMISSION, USAGE, BATTERY, OTA).
6. **Back from the dial** — `buddy_decision` (allow/deny), `buddy_ui` (bridge reads view/voice/sid), voice audio via ESPHome voice_assistant to the bridge (`bridge/voiceflow.mjs`, `voice.mjs`, `router.mjs`), which types into the session's iTerm tty.

## Files by concern

| Concern | Where |
|---|---|
| All UI timings, colours, state struct `Ui` | `buddy/ui_state.h` (constants at top) |
| Face view: running/idle face, card, bell pill, usage flash | `buddy/ui_face.h` |
| Reply card + Dismiss button, autoscroll | `buddy/ui_card.h` |
| Taps (`card`, `bell`, `next`/`prev`, allow/deny/later, ...) | `buddy/ui.h` `tap()` |
| Voice states on the dial | `buddy/ui_voice.h`; bridge side `bridge/voiceflow.mjs` |
| Which session the face shows | `buddy/sessions.h` (pure, `tests/test_sessions.cpp`) |
| Sleep/wake, PWR key | `buddy/power.h` (`tests/test_power.cpp`); raise to wake `buddy/raise_wake.h` (`tests/test_raise_wake.cpp`), fed by `buddy/ui.h` `motion()` |
| Hook entry, permission decide | `bridge/hook.sh` (Claude `PermissionRequest`; Copilot `copilot-gate` on `PreToolUse`, `bridge/hook.test.mjs`), `bridge/install.mjs` (writes the hook files) |
| Pet scenes / character packs | `buddy/scene.h`, `buddy/character.h`, `bridge/assets/`, `scripts/gen_buddy_pets.mjs` |
| Usage / battery / OTA pages | `buddy/ui_pages.h` |
| IMU eye gaze | `buddy/gaze.h` |
| Session state, hook event -> status | `bridge/board.mjs` (`board.test.mjs`) |
| Grok Bot as a pseudo-agent | `bridge/grokbot.mjs` |
| Voice routing ("tell X to ...") | `bridge/router.mjs` |
| Device YAML | `claude-buddy.yaml` includes `../esphome/chicken-base.yaml`, `dial-hardware.yaml`, `buddy-ui.yaml`; sim: `claude-buddy-sim.yaml` |
| Visual spec | `docs/HARNESS-UI-SPEC.md` (section numbers cited in comments) |

## Glossary

- **notice** — a reply from a session while other sessions are still working (`busy > 0` on the `turn` line). Queued in `notices_`, shown as the **bell** pill. Tap opens the oldest (`tap("bell")` pops it, sets `viewing`).
- **reply card** — `last_text_`, the finished turn's text. Moves to the bell after one autoscroll pass / `CARD_SHORT_MS` / `REPLY_TO_BELL_MS` (`ui_state.h`).
- **seen** — bridge evt sent on `UserPromptSubmit`; dial drops that sid's notices and its card. Currently the only "read on the Mac" signal. `SessionEnd` sends nothing.
- **read** — flag on a `turn` line: reply to words just sent by voice, so show it on the card, never as a notice.
- **focus_id** — the working session the face shows; sticky (swipe, bell or session end moves it).
- **viewing** — a notice's reply is on the card (opened from the bell) until Dismiss / card tap / swipe.
- **deferred_id** — a permission prompt put off with "later"; the bell reopens it when no notices are queued.

## Where to change X

- New bridge-to-dial event: `sendLine({evt:'x', ...})` in `dial-bridge.mjs` + `strcmp(evt, "x")` branch in `claude_buddy.cpp` + doc it in `docs/DIAL-PROTOCOL.md`.
- New tap target: `tap()` in `buddy/ui.h`; reachable remotely via API action `buddy_tap`.
- A timing: constant in `buddy/ui_state.h`.
- Per-device YAML change: override from `claude-buddy.yaml`, never edit the shared package.

## Run / verify

- `make test` — host C++ tests (`tests/`) + bridge node tests.
- `make sim` — sim build (only way; visible terminal). `make sim-panel` pins bots/states. Force a sim state with `build/sim-pin.jsonl`.
- `make flash` — OTA only (IP in `.env.dialip`). USB: `esphome run claude-buddy.yaml`. `make logs`.
- Bridge: `cd bridge && npm test`; daemon log `~/Library/Logs/dial-bridge.log`.
