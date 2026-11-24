# Dial bridge: every agent's status on the dial

Goal: the dial shows and answers sessions from the Claude Code CLI, Codex, Cursor, Gemini and other agents,
not only Claude desktop. It reuses three things that already exist:

- **The dial's Buddy protocol and UI** (`components/claude_buddy`, `buddy/`): the heartbeat → face / permission
  screen path works on hardware.
- **claude-control** (`~/workspace/claude-control`, Rob's fork of sverrirsig/claude-control, Next.js + Electron):
  - It already installs a Claude Code hook (`~/.claude-control/hooks/status-hook.sh`, `src/lib/hooks-installer.ts`).
  - It derives Working / Waiting / Idle per session.
  - It already approves and rejects prompts (`src/lib/actions.ts`, keystrokes via tmux or kitty).
- **peon-ping** (`~/.claude/hooks/peon-ping`, v2.24.0): adapters for Codex, Cursor, Gemini, Windsurf, Kiro,
  OpenCode and others. Each one translates its agent's events into Claude-shaped hook JSON (`hook_event_name`,
  `session_id`, `cwd`, …) and pipes it into `peon.sh` (`adapters/codex.sh:25`).

## Data flow

```
Claude Code hooks ─────────────────────────────┐
Codex/Cursor/Gemini… → peon-ping adapter        │  Claude-shaped hook JSON
   → $CLAUDE_PEON_DIR/peon.sh (shim) ──┬─→ real peon.sh (sounds, unchanged)
                                       └─→ claude-control status-hook.sh ─→ ~/.claude-control/events/
                                                                                   │
claude-control (Node) ── sessions + status ── src/lib/dial.ts ── heartbeat JSON ──┘
        ▲                                         │  ESPHome native API (Wi-Fi, Noise key)
        │ approve/reject (existing actions.ts)     ▼
        └──────── decision text_sensor ◄──── dial: buddy_line action → same parser/UI as BLE
```

## Pieces

1. **Dial (this repo), about 30 lines.**
   - Add an `api: actions: buddy_line(line: string)` that feeds `ClaudeBuddy::handle_line_`.
   - Lines arriving over the API mark the source as `API`. `live()` accepts a fresh API heartbeat with no BLE
     link.
   - A decision on an API-sourced prompt publishes a `text_sensor` `buddy_decision` = `"<prompt id> once|deny"`
     instead of a BLE notify.
   - BLE and the desktop keep working unchanged.
2. **Shim (`~/.dial-shim/peon.sh`), 3 lines.**
   - It tees stdin into the real `peon.sh` and into claude-control's `status-hook.sh`.
   - Each agent's adapter command gets `CLAUDE_PEON_DIR=~/.dial-shim`. Codex example:
     `notify = ["env", "CLAUDE_PEON_DIR=/Users/<you>/.dial-shim", "bash", ".../adapters/codex.sh"]`.
   - No peon-ping file is modified, so its updates don't clobber this.
   - The claude-control hook keys files by `$PPID`; for adapters that is the adapter's shell. Check that this
     still gives one stable file per agent session (otherwise key by `session_id`).
3. **claude-control `src/lib/dial.ts`, about 150 lines.**
   - It subscribes to the session list the dashboard already computes.
   - It folds that list into one Buddy heartbeat:
     - `total`, `running` (Working), `waiting` (Waiting).
     - `msg`: the newest working session's project and tool.
     - `entries`: the last five events, as `HH:MM project: event`.
     - `prompt`: the oldest Waiting session. `id = <pid>:<event ts>`, `tool`/`hint` from the JSONL tail.
   - It sends the heartbeat on change, plus a 10s keepalive, through `@2colors/esphome-native-api` (supports
     `encryptionKey`).
   - It maps a `buddy_decision` back to the existing approve/reject keystroke for that pid.
   - Dial host and key go in claude-control's settings.

## Order

1. Dial `buddy_line` + `buddy_decision`. Test with `aioesphomeapi` from a shell by sending `samples/heartbeat.json`.
2. claude-control `dial.ts`, Claude Code only (its hook is already installed).
3. The shim + Codex adapter, then the other agents.

## Open questions

- Agent identity on the face. Harness shows a per-agent name and pet. The Buddy heartbeat is aggregate, so the
  name arc could show the waiting or working session's project. The Codex pet art also exists in pets.c
  (`docs/harness-pets/assets/codex_*`) if per-agent faces are wanted later.
- claude-control is a fork (`origin` rgr4y, `upstream` sverrirsig). Do this on a new branch off `main`, not on
  `feat/terminal-output`, so it stays separate from the upstreamable work.
