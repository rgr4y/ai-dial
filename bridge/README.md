# dial-bridge

Shows every agent's status on the Claude Buddy dial (`../claude-buddy.yaml`) and answers Claude Code permission
prompts from it, over Wi-Fi. Protocol: [`../docs/DIAL-PROTOCOL.md`](../docs/DIAL-PROTOCOL.md).

- `dial-bridge.mjs`: the daemon. It takes hook events on `127.0.0.1:19876` and talks to the dial over the
  ESPHome native API (`DIAL_HOST`, a comma list: the dial and the desktop sim; key `api_key` from
  `~/workspace/esphome/secrets.yaml`). Every dial gets the same heartbeats; the first to answer a prompt wins. It also
  serves character packs (`assets/`) read-only on `0.0.0.0:19877` for the dial's LittleFS sync.
- `voiceflow.mjs`: the voice flow (hold, STT, route, confirm, send, the "Which session?" question, the speak queue) with
  injected dependencies; tested by `npm test`.
- `board.mjs`: session state turned into a heartbeat. Pure; tested by `npm test`.
- `hook.sh`: the hook command. It is fail-open: with no daemon or no dial, Claude Code behaves as if it weren't
  installed.
- `install.mjs`: registers `hook.sh` in `~/.claude/settings.json`, `hook.sh codex` in `~/.codex/hooks.json` and
  `hook.sh copilot` in `~/.copilot/settings.json` (busy-face events only), backing each file up first. Remove them with
  `node install.mjs --uninstall`.
- `peon-shim/peon.sh`: feeds peon-ping adapters (Codex, Cursor, Gemini, …) to both peon-ping and the bridge.
- `assets/`: character packs from `../scripts/gen_buddy_pets.mjs` (regenerated on every ESPHome compile).
- `com.rob.dial-bridge.plist`: the LaunchAgent. Logs go to `~/Library/Logs/dial-bridge.log`.

## Run

```bash
npm install
node install.mjs                                    # Claude Code, Codex and Copilot CLI hooks
cp com.rob.dial-bridge.plist ~/Library/LaunchAgents/ && launchctl bootstrap gui/$(id -u) ~/Library/LaunchAgents/com.rob.dial-bridge.plist
npm run sim                                         # desktop simulator (SDL window), see below
```

Restart after editing the daemon: `launchctl kickstart -k gui/$(id -u)/com.rob.dial-bridge`.
Status: `curl -s localhost:19876/status`.

## Simulator

`../claude-buddy-sim.yaml` builds the dial's real UI (`../buddy-ui.yaml`: `buddy/ui.h`, LVGL, the `claude_buddy`
component) for the Mac with ESPHome's `host` platform, drawing into an SDL window (`brew install sdl2-compat`).
- **Run:** `make sim` from the repo root; it rebuilds and relaunches 2 s after UI or component sources change (`make flash` builds and OTAs the dial). The plist's
  `DIAL_HOST=10.0.1.46,127.0.0.1` makes the daemon drive it alongside the dial, so it shows the same live state.
- **Input:** the mouse is touch. PWR and BOOT: click the keys on the case edge (placed as on the real dial), or press `p` / `b`. The window shows the 466 px glass inside the case at true scale.
- **Pets:** read straight from `assets/` (no LittleFS, no sync); restart the sim after regenerating.
- **Pin a scenario:** while `../build/sim-pin.jsonl` exists, the sim ignores the bridge and plays that file: one
  `buddy_line` JSON per line (docs/DIAL-PROTOCOL.md), all lines when the file changes, heartbeats again every 5 s.
  Delete it to hand back to the bridge. E.g. a busy Grok bot:
  `{"total":1,"running":1,"waiting":0,"work":[{"id":"s1","name":"3DBot","tool":"","avatar":"teardrop","avatar_color":16721468}],"latest":"s1","bots":[["teardrop",16721468,"3DBot"]]}`

## Voice

Hold the dial's face, speak, release. The dial streams the mic to the bridge (ESPHome `voice_assistant`, the bridge is
its API client), the bridge relays it to a Wyoming STT server (`VOICE_STT_HOST`, `VOICE_STT_PORT`; default
127.0.0.1:10300), the words show on the dial's reply card, and 2 s after the final text appears (tap the card to
cancel) the bridge types it plus Return into the iTerm tab of the session the dial was showing (matched by the tty
`hook.sh` records). Details and limits: docs/DIAL-PROTOCOL.md section 5. First run: macOS asks to let the bridge
control iTerm (Automation); the bridge runs under launchd, so approve it for that process. Only `voice.mjs` touches iTerm.

**Router.** Each final transcript goes through `router.mjs`. Rules first, no model: "tell api to ...", "api, ..." or a
leading session name types the rest into that session (spoken names match ignoring case, spaces and hyphens: "auto ai" =
`auto-ai`); a bare allow/yes or deny/no with exactly one open prompt answers it. Anything else is one `/v1/messages`
call through cli-proxy-api (`ANTHROPIC_BASE_URL`, `ANTHROPIC_API_KEY`: env only, never in a file; model
`VOICE_ROUTER_MODEL`, default `gemini-3-flash`, ~1 s) asking for JSON `{target, text}`: a session name, `focused`, or
`assistant`. `assistant` types into the tmux session `voice` (`make voice` starts `claude` in `~/workspace/voice-assistant`,
whose CLAUDE.md tells it how to read `GET /status` and reach other sessions); not running = card "Assistant offline", never
re-routed. A failed call, an HTTP error, junk, or no answer within `VOICE_ROUTER_TIMEOUT_MS` (3000) is logged
(`voice: router fallback (...)`) and the text goes to the focused session, or to the assistant when nothing is on the face.
The router can only pick sessions in the bridge's list; a name that matches none gets a spoken question back. `focused`
with nothing on the face shows "Which session? Say its name". Prompt answers wait out the same 2 s tap-to-cancel as typing.
A `speak` reply is also said on the dial speaker once it is sent (see Speaking).

**Speaking.** `speak(dial, text)` in `dial-bridge.mjs`: text goes through the claude-speak rules (`speak-rules.json`,
`~/.config/claude-speak/` first, else `~/workspace/claude-speak/`; only its filter is reused, none of its servers), then a
Wyoming `synthesize` to the say server (`VOICE_TTS_HOST` / `VOICE_TTS_PORT`, 127.0.0.1:10200; it answers 16 kHz 16-bit
mono, anything else is resampled / downmixed). The WAV is served at `http://<bridge ip>:19877/tts/<id>.wav` (deleted after
60 s) and the dial gets a `MediaPlayerCommandRequest` with `media_url` + `announcement`; the firmware's `on_announcement`
turns the amp on and `on_idle` off. The promise resolves true when the dial's media_player went announcing then left it
(that is the "done" signal), false on timeout or no dial. Try it: `curl -d '{"text":"Which session?"}' 127.0.0.1:19876/speak`.

In the sim there is no mic: holding the face fakes the words ("fix the failing test in auth module", one word per
400 ms), and the final 0.7 s after release. Pin a session first (`make sim-panel`: "3 sessions" or "Reply card"), or
the hold says "No session to talk to". The fake final never reaches iTerm: the bridge ignores a `send` it has no text for.

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
