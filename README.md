# autonomous-ai

Teardown notes for the Autonomous "Harness" device (a Waveshare ESP32-S3-Touch-AMOLED-1.75C in a printed base).

- `docs/PINMAP.md` lists the GPIO and I2C map, compares it with the Harness firmware, and covers flashing the stock firmware.
- `vendor/` holds the upstream clones: Waveshare's 1.75C repo and the openharness firmware (sparse checkout).
- `esp32-dial.yaml` is the ESPHome base config for the board, covering all of its hardware. It includes `../esphome/chicken-base.yaml`, and `secrets.yaml` is a symlink to `~/workspace/esphome/secrets.yaml`.
- `claude-buddy.yaml` turns the dial into a Claude Buddy for every agent, over Wi-Fi.
  - It draws Harness's Claude face (pets, curved name and status text with the status shimmer, cards) and a
    permission screen with allow/deny. The status comes from `bridge/`, a Node daemon fed by Claude Code hooks
    and peon-ping adapters.
  - Board package: `dial-hardware.yaml`. Component: `components/claude_buddy/`. UI: `buddy/`.
  - Docs: `docs/DIAL-PROTOCOL.md` (protocol), `docs/HARNESS-UI-SPEC.md` (UI spec), `bridge/README.md` (daemon
    and simulator).
  - UI package shared with the simulator: `buddy-ui.yaml`. Simulator: `claude-buddy-sim.yaml` (ESPHome `host` +
    SDL, the same C++ UI): `make sim`.
  - Tests: `make test` (`test:cpp`: host C++ `tests/test_*.cpp`: scene maths, PWR sleep/wake + sim key hit-test; `test:node`: bridge `node --test`).
- `docs/firmware-backup/` (local only, gitignored) holds the full 32 MB dump of the original Harness firmware, with the restore command.
