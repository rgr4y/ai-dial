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
#
# `hook.sh copilot-gate` is the Copilot PreToolUse gate. Copilot never fires a PermissionRequest hook (only the eight
# busy events), so PreToolUse is the only place to put the dial in the loop. It always posts the busy event (agent
# "copilot"), and for a tool that mutates something (bash, edit, write, apply_patch, ...) it also holds on the dial
# and answers with a permissionDecision. Reads pass straight through, so the dial is not prompted for every glob.
# Fail-open: no answer means no output, and Copilot decides as it normally would.
URL="http://127.0.0.1:${DIAL_BRIDGE_PORT:-19876}"
INPUT=$(cat)
GATE=0
[ "$1" = "copilot-gate" ] && GATE=1 && set -- copilot
[ -n "$1" ] && INPUT="{\"agent\":\"$1\",${INPUT#*\{}"
TTY=$(ps -o tty= -p $PPID 2>/dev/null | tr -d ' ')
case "$TTY" in '' | '?' | '??') ;; *) INPUT="{\"tty\":\"$TTY\",${INPUT#*\{}" ;; esac

if [ "$GATE" = 1 ]; then
  # Always post the busy event (agent "copilot"), and normalise the two payload shapes in the wild in one pass:
  # VS Code's extension sends Claude-shaped {hook_event_name,tool_name,tool_input}; Copilot CLI sends camelCase
  # {toolCalls:[{name,args}]}. THEN_MUTATE=1 when any tool mutates, which is what earns a dial prompt.
  NORM=$(printf '%s' "$INPUT" | python3 -c '
import sys, json
d = json.load(sys.stdin)
# Copilot CLI sends camelCase keys; add the Claude-shaped event name so the daemon/board recognises it either way.
if not d.get("hook_event_name"): d["hook_event_name"] = "PreToolUse"
tools = [d.get("tool_name")] if d.get("tool_name") else [t.get("name") for t in (d.get("toolCalls") or [])]
tools = [t for t in tools if t]
if tools and not d.get("tool_name"): d["tool_name"] = tools[0]
if tools and not d.get("tool_input"): d["tool_input"] = (d.get("toolCalls") or [{}])[0].get("args", {})
MUT = {"bash","powershell","shell","write","edit","multi_edit","multiedit","str_replace","apply_patch","create",
       "create_file","notebook_edit","notebookedit","delete","move","rename"}
d["_gate"] = 1 if any(str(t).lower() in MUT for t in tools) else 0
json.dump(d, sys.stdout)
' 2>/dev/null) || NORM="$INPUT"
  printf '%s' "$NORM" | curl -sf -m 1 -H 'content-type: application/json' --data-binary @- "$URL/event" >/dev/null 2>&1 || true
  case "$NORM" in *'"_gate":1'* | *'"_gate": 1'*) ;; *) exit 0 ;; esac
  REPLY=$(printf '%s' "$NORM" | curl -sf -m "${DIAL_GATE_TIMEOUT_S:-290}" -H 'content-type: application/json' \
    --data-binary @- "$URL/permission" 2>/dev/null) || exit 0
  case "$REPLY" in
    *'"allow"'*) printf '%s\n' '{"hookSpecificOutput":{"hookEventName":"PreToolUse","permissionDecision":"allow"}}' ;;
    *'"deny"'*) printf '%s\n' '{"hookSpecificOutput":{"hookEventName":"PreToolUse","permissionDecision":"deny","permissionDecisionReason":"Denied on the dial"}}' ;;
  esac
  exit 0
fi

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
