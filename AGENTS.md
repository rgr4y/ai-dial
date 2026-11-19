# Agent rules

- **Commits:** when asked to commit, first list any edits Rob hasn't approved and ask approve or hold for each. Commit only approved ones, one concern per commit. A small follow-up to an unpushed commit can go in as a fixup of it instead.
- **UI changes:** show them in the sim (`make sim`) and get approval before flashing the dial.
- **Builds:** compile, flash and run the sim only in a visible Terminal (`make sim`, `make flash`), never hidden.

## ESPHome YAML: extend, don't fork

`claude-buddy.yaml` pulls in packages (`../esphome/chicken-base.yaml`, `dial-hardware.yaml`, `buddy-ui.yaml`). Never edit a shared package to change one device; override it from the including file. Merge rules (ESPHome `config_helpers.py` merge_config, `config.py` resolve_extend_remove):
- Maps merge key by key, recursively. A scalar set later wins.
- Lists concatenate, so `ota: - platform: esphome` with only `on_begin:` etc. adds triggers to the base OTA. ESPHome folds esphome-OTA entries on the same port into one, keeping the password.
- `- id: !extend <id>` merges into the list item with that id (e.g. `touchscreen: - id: !extend touch` in buddy-ui.yaml).
- `- id: !remove <id>` drops that list item.
- `<key>: !remove` drops a key at any depth, a whole top-level component included (`web_server: !remove`).

## Task tracking: beads (`br`)

Work items are beads in `.beads/`, kept locally and gitignored. `bv` gives a viewer and graph.

- **Find work:** `br ready` lists open, unblocked beads. `br show <id>` gives the full description, which is the whole spec.
- **Claim:** `br update <id> --status=in_progress` before you start.
- **Scope:** stay inside the files the bead names. If you need more, stop and run `br comments add <id> "<why>"`.
- **Finish:** run the bead's Verify steps, then `br close <id> -r "<what changed; what was verified; what Rob still has to check>"`.
- **New work you find:** create a bead (`br create ... -t task -p <0-4>`) instead of widening the current one.
- **Order:** dependencies serialize beads that touch the same files. Never work a blocked bead.
- **Rob's decision:** turning audits or reviews into beads (`/br-0`) and running the queue (`/br-1`) are Rob's call. Agents don't run them themselves.
- **Under `/br-1`:** the worker brief overrides **Finish** above. Workers don't commit or close. The orchestrator verifies, commits (`<summary> [<bead-id>]`), and closes.
