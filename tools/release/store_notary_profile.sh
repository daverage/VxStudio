#!/usr/bin/env bash
set -euo pipefail

PROFILE_NAME="${1:-${APPLE_NOTARY_PROFILE:-}}"

if [[ -z "${PROFILE_NAME}" ]]; then
  echo "Usage: $0 <profile-name>" >&2
  echo "Or set APPLE_NOTARY_PROFILE in the environment." >&2
  exit 1
fi

if ! command -v xcrun >/dev/null 2>&1; then
  echo "xcrun is required" >&2
  exit 1
fi

echo "Storing notary credentials under profile '${PROFILE_NAME}'"
echo "Use either:"
echo "  1. App Store Connect API key: APPLE_API_KEY_PATH + APPLE_API_KEY_ID + APPLE_API_ISSUER"
echo "  2. Apple ID flow: APPLE_ID + APPLE_TEAM_ID (+ optional APPLE_APP_SPECIFIC_PASSWORD)"

if [[ -n "${APPLE_API_KEY_PATH:-}" && -n "${APPLE_API_KEY_ID:-}" ]]; then
  cmd=(
    xcrun notarytool store-credentials "${PROFILE_NAME}"
    --key "${APPLE_API_KEY_PATH}"
    --key-id "${APPLE_API_KEY_ID}"
    --validate
  )
  if [[ -n "${APPLE_API_ISSUER:-}" ]]; then
    cmd+=(--issuer "${APPLE_API_ISSUER}")
  fi
  "${cmd[@]}"
  exit 0
fi

if [[ -z "${APPLE_ID:-}" || -z "${APPLE_TEAM_ID:-}" ]]; then
  echo "For Apple ID flow you must set APPLE_ID and APPLE_TEAM_ID." >&2
  exit 1
fi

cmd=(
  xcrun notarytool store-credentials "${PROFILE_NAME}"
  --apple-id "${APPLE_ID}"
  --team-id "${APPLE_TEAM_ID}"
  --validate
)

if [[ -n "${APPLE_APP_SPECIFIC_PASSWORD:-}" ]]; then
  cmd+=(--password "${APPLE_APP_SPECIFIC_PASSWORD}")
fi

"${cmd[@]}"

