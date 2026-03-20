#!/usr/bin/env bash
set -euo pipefail

STAGE_DIR="${1:-Source/vxsuite/vst}"
PROFILE_NAME="${APPLE_NOTARY_PROFILE:-}"

status=0

echo "Release preflight"
echo "Stage dir: ${STAGE_DIR}"

if [[ ! -d "${STAGE_DIR}" ]]; then
  echo "[FAIL] Stage directory not found: ${STAGE_DIR}" >&2
  exit 1
fi

pluginval_path=""
if command -v pluginval >/dev/null 2>&1; then
  pluginval_path="$(command -v pluginval)"
elif [[ -x "/Applications/pluginval.app/Contents/MacOS/pluginval" ]]; then
  pluginval_path="/Applications/pluginval.app/Contents/MacOS/pluginval"
fi

if [[ -n "${pluginval_path}" ]]; then
  echo "[OK] pluginval: ${pluginval_path}"
else
  echo "[WARN] pluginval not found on PATH"
  status=1
fi

if command -v security >/dev/null 2>&1; then
  identities="$(security find-identity -v -p codesigning 2>/dev/null || true)"
  echo "${identities}"
  if ! grep -q "Developer ID Application" <<<"${identities}"; then
    echo "[WARN] No Developer ID Application identity found in keychain"
    status=1
  else
    echo "[OK] Developer ID Application identity present"
  fi
fi

if [[ -n "${PROFILE_NAME}" ]]; then
  if command -v xcrun >/dev/null 2>&1; then
    if xcrun notarytool history --keychain-profile "${PROFILE_NAME}" >/dev/null 2>&1; then
      echo "[OK] notary profile '${PROFILE_NAME}' is usable"
    else
      echo "[WARN] notary profile '${PROFILE_NAME}' is not usable yet"
      status=1
    fi
  fi
else
  echo "[WARN] APPLE_NOTARY_PROFILE is not set"
  status=1
fi

if [[ -d "${STAGE_DIR}/VXDeepFilterNet.vst3/Contents/Resources" ]]; then
  missing=0
  for model in DeepFilterNet2_onnx.tar.gz DeepFilterNet2_onnx_ll.tar.gz DeepFilterNet3_onnx.tar.gz; do
    if [[ ! -f "${STAGE_DIR}/VXDeepFilterNet.vst3/Contents/Resources/${model}" ]]; then
      echo "[WARN] Missing DeepFilterNet resource: ${model}"
      missing=1
      status=1
    fi
  done
  if [[ ${missing} -eq 0 ]]; then
    echo "[OK] DeepFilterNet bundle resources present"
  fi
fi

exit ${status}
