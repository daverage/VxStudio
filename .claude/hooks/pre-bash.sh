#!/usr/bin/env bash
# PreToolUse:Bash — blocks destructive shell commands. exit 2 = blocking error to Claude.
INPUT=$(cat)
CMD=$(echo "$INPUT" | python3 -c \
  "import sys,json; d=json.load(sys.stdin); print(d.get('tool_input',{}).get('command',''))" 2>/dev/null)
PATTERNS=(
  "rm -rf /" "rm -rf ~" "rm -rf \$HOME" ":(){:|:&};:" "mkfs"
  "dd if=/dev/zero" "> /dev/sd" "curl.* | bash" "curl.* | sh"
  "wget.* | bash" "wget.* | sh" "chmod -R 777 /" "chown -R.* /"
)
for p in "${PATTERNS[@]}"; do
  if echo "$CMD" | grep -qiE "$p"; then
    echo "BLOCKED: Dangerous pattern matched: $p" >&2; echo "Command: $CMD" >&2; exit 2
  fi
done
if echo "$CMD" | grep -qE "git push.*(--force|-f).*(main|master|production|prod|release)"; then
  echo "BLOCKED: Force-push to protected branch not allowed." >&2; exit 2
fi
exit 0
