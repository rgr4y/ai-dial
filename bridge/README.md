# dial-bridge

Shows every agent's status on the Claude Buddy dial (`../claude-buddy.yaml`) and answers Claude Code permission
prompts from it, over Wi-Fi. Protocol: [`../docs/DIAL-PROTOCOL.md`](../docs/DIAL-PROTOCOL.md).

- `dial-bridge.mjs`: the daemon. It takes hook events on `127.0.0.1:19876` and talks to the dial over the
  ESPHome native API (`DIAL_HOST`, a comma list: the dial and the desktop sim; key `api_key` from
  `~/workspace/esphome/secrets.yaml`). Every dial gets the same heartbeats; the first to answer a prompt wins. It also
  serves character packs (`assets/`) read-only on `0.0.0.0:19877` for the dial's LittleFS sync.
- `board.mjs`: session state turned into a heartbeat. Pure; tested by `npm test`.
- `hook.sh`: the hook command. It is fail-open: with no daemon or no dial, Claude Code behaves as if it weren't
  installed.
- `install.mjs`: registers `hook.sh` in `~/.claude/settings.json` and backs the file up first. Remove it with
  `node install.mjs --uninstall`.
- `peon-shim/peon.sh`: feeds peon-ping adapters (Codex, Cursor, Gemini, …) to both peon-ping and the bridge.
- `assets/`: character packs from `../scripts/gen_buddy_pets.mjs` (regenerated on every ESPHome compile).
- `com.rob.dial-bridge.plist`: the LaunchAgent. Logs go to `~/Library/Logs/dial-bridge.log`.

## Run

```bash
npm install
node install.mjs                                    # Claude Code hooks
cp com.rob.dial-bridge.plist ~/Library/LaunchAgents/ && launchctl bootstrap gui/$(id -u) ~/Library/LaunchAgents/com.rob.dial-bridge.plist
npm run sim                                         # desktop simulator (SDL window), see below
```

Restart after editing the daemon: `launchctl kickstart -k gui/$(id -u)/com.rob.dial-bridge`.
Status: `curl -s localhost:19876/status`.

## Simulator

`../claude-buddy-sim.yaml` builds the dial's real UI (`../buddy-ui.yaml`: `buddy/ui.h`, LVGL, the `claude_buddy`
component) for the Mac with ESPHome's `host` platform, drawing into an SDL window (`brew install sdl2-compat`).
- **Run:** `make sim` from the repo root; it rebuilds and relaunches 2 s after UI or component sources change (`make flash` builds and OTAs the dial). The plist's
  `DIAL_HOST=10.0.0.11,127.0.0.1` makes the daemon drive it alongside the dial, so it shows the same live state.
- **Input:** the mouse is touch. PWR and BOOT: click the keys on the case edge (placed as on the real dial), or press `p` / `b`. The window shows the 466 px glass inside the case at true scale.
- **Pets:** read straight from `assets/` (no LittleFS, no sync); restart the sim after regenerating.

## Grok Bot

The bridge also watches Grok Bot's local store (`~/Library/Application Support/Grok Bot/sand-client-persistence/`,
read-only) and feeds it through `grokbot.mjs` as `grok-bot:` sessions. Override the dir with `GROK_BOT_STORE`; if it
doesn't exist the bridge logs once and carries on. History at startup isn't replayed. On the dial: your prompt ->
working, the bot's reply -> reply card, a local command -> an entry (`<bot>: run-command <cmd>`). No permission
answering yet. The log gets one line per batch (`grok <bot>: <n> events`), never message content.

## Usage

Plan usage for every provider comes from CodexBar's HTTP server (Settings → Advanced, port 6969 here; set
`CODEXBAR_URL` in the plist if it moves). The dial rotates providers, one per flash. If CodexBar is down, the
bridge shows Claude only, via `~/.claude/refresh-usage.sh`.

## Codex and the other agents

Point the agent's peon-ping adapter at the shim. Codex, in `~/.codex/config.toml`:
```toml
notify = ["env", "CLAUDE_PEON_DIR=/Users/rob/workspace/autonomous-ai/bridge/peon-shim", "bash",
          "/Users/rob/.claude/hooks/peon-ping/adapters/codex.sh"]
```
This isn't wired yet: your Codex `notify` already chains Computer Use and oh-my-codex. Codex's notify fires after
the fact, so its sessions get status only, with no approvals from the dial.
