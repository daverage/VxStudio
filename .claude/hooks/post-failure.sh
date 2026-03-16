#!/usr/bin/env bash
# PostToolUseFailure — logs failed tool calls for diagnostics.
LOG="${CLAUDE_PROJECT_DIR:-$PWD}/.claude/tool-failures.log"
INPUT=$(cat)
TOOL=$(echo "$INPUT" | python3 -c \
  "import sys,json; d=json.load(sys.stdin); print(d.get('tool_name','unknown'))" 2>/dev/null)
mkdir -p "$(dirname "$LOG")"
echo "[$(date -u +"%Y-%m-%dT%H:%M:%SZ")] tool=$TOOL" >> "$LOG"
echo "$INPUT" >> "$LOG"
echo "---" >> "$LOG"
exit 0
