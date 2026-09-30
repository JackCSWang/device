#!/usr/bin/env bash
# Drag-install .app. Gatekeeper BLOCKS unsigned apps outright, so signing
# and notarization are required, not optional (spec 12).
#
# UNVERIFIED: this machine has no macOS toolchain. Written to spec, never run.
set -euo pipefail
BUILD=${1:-build}

cmake -B "$BUILD" -DCMAKE_BUILD_TYPE=Release
cmake --build "$BUILD"

# qt_standard_project_setup() (root CMakeLists.txt) points runtime target
# outputs -- including a MACOSX_BUNDLE target's .app -- at the top of the
# build tree, not at $BUILD/src/ (same root cause as the Windows script's
# equivalent fix; see packaging/windows/build-portable.sh).
APP="$BUILD/microscope.app"
macdeployqt "$APP" -qmldir=src/ui

if [[ -n "${CODESIGN_IDENTITY:-}" ]]; then
  codesign --deep --force --options runtime \
           --entitlements packaging/macos/entitlements.plist \
           --sign "$CODESIGN_IDENTITY" "$APP"
  echo "Signed. Now notarize:"
  echo "  xcrun notarytool submit --wait <zip of $APP>"
else
  echo "WARNING: CODESIGN_IDENTITY unset. Gatekeeper will block this build."
fi
