#!/usr/bin/env bash
# PreToolUse:Edit|Write — blocks writes to secrets and lock files. exit 2 = blocking.
INPUT=$(cat)
FILE=$(echo "$INPUT" | python3 -c \
  "import sys,json; d=json.load(sys.stdin); print(d.get('tool_input',{}).get('path',''))" 2>/dev/null)
[ -z "$FILE" ] && exit 0
SENSITIVE=(\.env$ \.env\. id_rsa id_ed25519 id_ecdsa \.pem$ \.key$ \.p12$ \.pfx$ secrets\. credentials\. \.netrc$ auth\.json$)
for p in "${SENSITIVE[@]}"; do
  echo "$FILE" | grep -qiE "$p" && echo "BLOCKED: Sensitive file: $FILE" >&2 && exit 2
done
LOCKFILES=(package-lock\.json$ yarn\.lock$ pnpm-lock\.yaml$ Cargo\.lock$ Gemfile\.lock$ poetry\.lock$ composer\.lock$ go\.sum$)
for p in "${LOCKFILES[@]}"; do
  echo "$FILE" | grep -qiE "$p" && echo "BLOCKED: Do not hand-edit lock files: $FILE" >&2 && exit 2
done
exit 0
