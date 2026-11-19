# dial-bridge

Shows every agent's status on the Claude Buddy dial (`../claude-buddy.yaml`) and answers Claude Code permission
prompts from it, over Wi-Fi. Protocol: [`../docs/DIAL-PROTOCOL.md`](../docs/DIAL-PROTOCOL.md).

- `dial-bridge.mjs`: the daemon. It takes hook events on `127.0.0.1:19876` and talks to the dial over the
  ESPHome native API (host `claude-buddy.local`, key `api_key` from `~/workspace/esphome/secrets.yaml`). It also
  serves the simulator WebSocket at `/ws`.
- `board.mjs`: session state turned into a heartbeat. Pure; tested by `npm test`.
- `hook.sh`: the hook command. It is fail-open: with no daemon or no dial, Claude Code behaves as if it weren't
  installed.
- `install.mjs`: registers `hook.sh` in `~/.claude/settings.json` and backs the file up first. Remove it with
  `node install.mjs --uninstall`.
- `peon-shim/peon.sh`: feeds peon-ping adapters (Codex, Cursor, Gemini, …) to both peon-ping and the bridge.
- `sim/`: the simulator.
- `com.rob.dial-bridge.plist`: the LaunchAgent. Logs go to `~/Library/Logs/dial-bridge.log`.

## Run

```bash
npm install
node install.mjs                                    # Claude Code hooks
cp com.rob.dial-bridge.plist ~/Library/LaunchAgents/ && launchctl bootstrap gui/$(id -u) ~/Library/LaunchAgents/com.rob.dial-bridge.plist
npm run dev                                         # simulator: http://localhost:5180 (hot reload)
```

Restart after editing the daemon: `launchctl kickstart -k gui/$(id -u)/com.rob.dial-bridge`.
Status: `curl -s localhost:19876/status`.

## Simulator

A Vite page that redraws what the dial reports it is showing (`ui` JSON), using the same Harness pet art
(`../docs/harness-pets`) and maths.
- **Taps:** a click is a tap, holding for 650 ms is a hold, and dragging right is a swipe back. PWR and BOOT
  buttons sit under the glass.
- **Sync:** taps go to the dial, and the dial's new state comes back, so the page and the glass stay in step. A
  touch on the dial shows up on the page as well.

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
