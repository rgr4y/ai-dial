#!/bin/bash
# Point a peon-ping adapter at this directory (CLAUDE_PEON_DIR=.../bridge/peon-shim) and every event it translates
# goes to both peon-ping (sounds, unchanged) and the dial bridge. peon-ping's own files are never edited.
# Codex example, ~/.codex/config.toml:
#   notify = ["env", "CLAUDE_PEON_DIR=/path/to/autonomous-ai/bridge/peon-shim", "bash",
#             "/Users/<you>/.claude/hooks/peon-ping/adapters/codex.sh"]
INPUT=$(cat)
printf '%s' "$INPUT" | bash "$HOME/.claude/hooks/peon-ping/peon.sh"
printf '%s' "$INPUT" | "$(dirname "$0")/../hook.sh"
