#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build"
OUT_DIR="${REPO_ROOT}/../../outputs/lvgl-preview"

cmake -S "${SCRIPT_DIR}" -B "${BUILD_DIR}"
cmake --build "${BUILD_DIR}" --target render_ui -j
mkdir -p "${OUT_DIR}"
"${BUILD_DIR}/render_ui" "${OUT_DIR}"

if command -v sips >/dev/null 2>&1; then
  sips -s format png "${OUT_DIR}/dashboard.ppm" --out "${OUT_DIR}/dashboard.png" >/dev/null
  sips -s format png "${OUT_DIR}/activity.ppm" --out "${OUT_DIR}/activity.png" >/dev/null
fi

echo "${OUT_DIR}"
