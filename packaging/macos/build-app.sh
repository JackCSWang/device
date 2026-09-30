#!/usr/bin/env bash
# Drag-install .app. Gatekeeper BLOCKS unsigned apps outright, so signing
# and notarization are required, not optional (spec 12).
#
# UNVERIFIED: this machine has no macOS toolchain. Written to spec, never run.
set -euo pipefail
BUILD=${1:-build}

cmake -B "$BUILD" -DCMAKE_BUILD_TYPE=Release
cmake --build "$BUILD"

APP="$BUILD/src/microscope.app"
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
