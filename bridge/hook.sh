#!/bin/bash
# dial-bridge hook. Claude Code runs it for each hook event; peon-ping's adapters (Codex, Cursor, Gemini, ...)
# reach it through peon-shim.sh. Reads hook JSON on stdin and forwards it to the dial-bridge daemon.
#
# PermissionRequest blocks until the dial answers, then prints Claude Code's decision JSON. Claude Code shows its
# own prompt at the same time and takes whichever answer comes first. Everything is fail-open: no daemon, no dial
# or no answer means no output, and Claude Code carries on as if this hook didn't exist.
# An argument names the agent (`hook.sh codex` from ~/.codex/hooks.json, `hook.sh copilot` from
# ~/.copilot/settings.json): it goes in as "agent", so the dial can show that agent's pet while it works. Claude Code
# passes none. The session's terminal goes in as "tty" (`ttys003`) when the parent process has one, so the dial can
# type into it.
URL="http://127.0.0.1:${DIAL_BRIDGE_PORT:-19876}"
INPUT=$(cat)
[ -n "$1" ] && INPUT="{\"agent\":\"$1\",${INPUT#*\{}"
TTY=$(ps -o tty= -p $PPID 2>/dev/null | tr -d ' ')
case "$TTY" in '' | '?' | '??') ;; *) INPUT="{\"tty\":\"$TTY\",${INPUT#*\{}" ;; esac
case "$INPUT" in
  *'"hook_event_name":"PermissionRequest"'* | *'"hook_event_name": "PermissionRequest"'*)
    REPLY=$(printf '%s' "$INPUT" | curl -sf -m "${DIAL_PROMPT_TIMEOUT_S:-290}" -H 'content-type: application/json' \
      --data-binary @- "$URL/permission" 2>/dev/null) || exit 0
    case "$REPLY" in
      *'"allow"'*) printf '%s\n' '{"hookSpecificOutput":{"hookEventName":"PermissionRequest","decision":{"behavior":"allow"}}}' ;;
      *'"deny"'*) printf '%s\n' '{"hookSpecificOutput":{"hookEventName":"PermissionRequest","decision":{"behavior":"deny","message":"Denied on the dial"}}}' ;;
    esac
    ;;
  *)
    printf '%s' "$INPUT" | curl -sf -m 1 -H 'content-type: application/json' --data-binary @- "$URL/event" \
      >/dev/null 2>&1 || true
    ;;
esac
exit 0
