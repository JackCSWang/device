#!/usr/bin/env bash
# AppImage: chmod +x and run. No package manager, no sudo (spec 12).
#
# UNVERIFIED: this machine has no Linux toolchain. Written to spec, never run.
set -euo pipefail
BUILD=${1:-build}

# `linuxdeploy --icon-file` below needs a real 256x256 PNG that does not yet
# exist in this repo (deliberately not fabricated -- an app icon is a real
# asset decision, not something to guess at). Fail here, clearly, instead of
# letting `cmake --install` and `linuxdeploy` run first and fail on an
# obscure "file not found" partway through.
ICON=packaging/linux/microscope.png
if [[ ! -f "$ICON" ]]; then
  echo "ERROR: $ICON is missing." >&2
  echo "Add a 256x256 PNG app icon there before running this script" \
       "(see docs/manual-test-matrix.md, Known hardware facts)." >&2
  exit 1
fi

cmake -B "$BUILD" -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr
cmake --build "$BUILD"
# src/CMakeLists.txt's install(TARGETS microscope ...) is what makes this
# populate AppDir at all -- `cmake --install` with no install() rules exits
# 0 and installs nothing, leaving linuxdeploy with no executable to package.
DESTDIR=AppDir cmake --install "$BUILD"

linuxdeploy --appdir AppDir \
            --plugin qt \
            --output appimage \
            --desktop-file packaging/linux/microscope.desktop \
            --icon-file "$ICON"
echo "Built Microscope-x86_64.AppImage"
