#!/usr/bin/env bash
set -euo pipefail

STAGE_DIR="${1:-Source/vxsuite/vst}"

if [[ ! -d "${STAGE_DIR}" ]]; then
  echo "Stage directory not found: ${STAGE_DIR}" >&2
  exit 1
fi

if [[ -z "${APPLE_DEVELOPER_IDENTITY:-}" ]]; then
  echo "APPLE_DEVELOPER_IDENTITY is required" >&2
  exit 1
fi

if ! command -v codesign >/dev/null 2>&1; then
  echo "codesign is required" >&2
  exit 1
fi

mapfile -t bundles < <(find "${STAGE_DIR}" -maxdepth 1 -type d -name '*.vst3' | sort)

if [[ ${#bundles[@]} -eq 0 ]]; then
  echo "No .vst3 bundles found in ${STAGE_DIR}" >&2
  exit 1
fi

for bundle in "${bundles[@]}"; do
  echo "Signing ${bundle}"
  codesign --force --deep --timestamp --options runtime --sign "${APPLE_DEVELOPER_IDENTITY}" "${bundle}"
done

if [[ -n "${APPLE_NOTARY_PROFILE:-}" ]]; then
  if ! command -v xcrun >/dev/null 2>&1; then
    echo "xcrun is required for notarization" >&2
    exit 1
  fi

  for bundle in "${bundles[@]}"; do
    echo "Submitting ${bundle} for notarization"
    xcrun notarytool submit "${bundle}" --keychain-profile "${APPLE_NOTARY_PROFILE}" --wait
    echo "Stapling ${bundle}"
    xcrun stapler staple "${bundle}"
    xcrun stapler validate "${bundle}"
  done
else
  echo "APPLE_NOTARY_PROFILE not set; signing completed but notarization was skipped"
fi

