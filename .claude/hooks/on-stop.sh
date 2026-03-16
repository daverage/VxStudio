#!/usr/bin/env bash
# Stop hook — prints a git diff summary at the end of each Claude turn.
cd "${CLAUDE_PROJECT_DIR:-$PWD}" 2>/dev/null || exit 0
if git rev-parse --git-dir >/dev/null 2>&1; then
  CHANGED=$(git diff --stat HEAD 2>/dev/null | tail -1)
  UNTRACKED=$(git ls-files --others --exclude-standard 2>/dev/null | wc -l | tr -d ' ')
  if [ -n "$CHANGED" ] || [ "${UNTRACKED:-0}" -gt 0 ]; then
    echo ""
    echo "=== Turn summary ================================================="
    [ -n "$CHANGED" ]              && echo "  Modified : $CHANGED"
    [ "${UNTRACKED:-0}" -gt 0 ]    && echo "  New files: $UNTRACKED untracked"
    echo "================================================================="
  fi
fi
exit 0
