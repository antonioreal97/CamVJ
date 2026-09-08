#!/usr/bin/env bash
# Build a drag-to-Applications DMG from the Release app bundle.
#
# Does not rename the binary inside the bundle (stays atem_fx) or change the
# bundle id (fx.atem.engine) — only the outer .app folder is CamVJ.app.
#
# Usage:
#   ./scripts/package_macos.sh
#   ./scripts/package_macos.sh -B build -v 0.1.0
#   ./scripts/package_macos.sh --skip-smoke

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${ROOT}/build"
VERSION=""
SKIP_SMOKE=0

usage() {
    cat <<'EOF'
Usage: package_macos.sh [-B BUILD_DIR] [-v VERSION] [--skip-smoke] [-h]

  -B BUILD_DIR   CMake build directory (default: ./build)
  -v VERSION     Package version (default: from CMakeLists.txt)
  --skip-smoke   Do not run the headless 200-frame check on the stage
  -h             Show this help
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        -B)
            BUILD_DIR="$(cd "$2" && pwd)"
            shift 2
            ;;
        -v)
            VERSION="$2"
            shift 2
            ;;
        --skip-smoke)
            SKIP_SMOKE=1
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "unknown argument: $1" >&2
            usage >&2
            exit 2
            ;;
    esac
done

if [[ -z "${VERSION}" ]]; then
    VERSION="$(sed -nE 's/^project\(AtemFx VERSION ([0-9]+\.[0-9]+\.[0-9]+).*/\1/p' \
        "${ROOT}/CMakeLists.txt" | head -1)"
fi
if [[ -z "${VERSION}" ]]; then
    echo "could not read PROJECT_VERSION from CMakeLists.txt; pass -v" >&2
    exit 1
fi

SRC_APP="${BUILD_DIR}/bin/atem_fx.app"
if [[ ! -d "${SRC_APP}" ]]; then
    echo "missing ${SRC_APP}" >&2
    echo "build Release first:" >&2
    echo "  cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j" >&2
    exit 1
fi
if [[ ! -x "${SRC_APP}/Contents/MacOS/atem_fx" ]]; then
    echo "missing executable inside ${SRC_APP}" >&2
    exit 1
fi

DIST_DIR="${ROOT}/dist"
STAGE_ROOT="${DIST_DIR}/macos"
STAGE_APP="${STAGE_ROOT}/CamVJ.app"
DMG_NAME="CamVJ-${VERSION}-macos.dmg"
DMG_PATH="${DIST_DIR}/${DMG_NAME}"
VOL_NAME="CamVJ ${VERSION}"

rm -rf "${STAGE_ROOT}"
mkdir -p "${STAGE_ROOT}"

# ditto preserves bundle metadata better than cp -R on APFS.
ditto "${SRC_APP}" "${STAGE_APP}"

if [[ ! -d "${STAGE_APP}/Contents/Resources/shaders" ]]; then
    echo "staged app has no Contents/Resources/shaders — rebuild with atem_fx_shaders" >&2
    exit 1
fi

if [[ "${SKIP_SMOKE}" -eq 0 ]]; then
    echo "smoke: headless 200 frames from stage…"
    "${STAGE_APP}/Contents/MacOS/atem_fx" --headless --frames 200
fi

# DMG staging: app + Applications symlink (Finder drag-install layout).
DMG_STAGE="${DIST_DIR}/macos-dmg"
rm -rf "${DMG_STAGE}"
mkdir -p "${DMG_STAGE}"
ditto "${STAGE_APP}" "${DMG_STAGE}/CamVJ.app"
ln -s /Applications "${DMG_STAGE}/Applications"

rm -f "${DMG_PATH}"
# UDZO: compressed read-only image; no external create-dmg dependency.
hdiutil create \
    -volname "${VOL_NAME}" \
    -srcfolder "${DMG_STAGE}" \
    -ov \
    -format UDZO \
    "${DMG_PATH}"

rm -rf "${DMG_STAGE}"

echo "wrote ${DMG_PATH}"
ls -lh "${DMG_PATH}"
