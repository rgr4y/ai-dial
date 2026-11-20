#!/bin/sh
# Send the arguments into the Claude session's notify channel (.claude/channel/notify.ts). Silent no-op if none is listening.
#   ./notify.sh flash done
curl -s -m 2 -X POST "localhost:${NOTIFY_PORT:-8788}" --data-binary "$*" >/dev/null || true
