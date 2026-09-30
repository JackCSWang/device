#!/usr/bin/env bash
# AppImage: chmod +x and run. No package manager, no sudo (spec 12).
#
# UNVERIFIED: this machine has no Linux toolchain. Written to spec, never run.
set -euo pipefail
BUILD=${1:-build}

cmake -B "$BUILD" -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr
cmake --build "$BUILD"
DESTDIR=AppDir cmake --install "$BUILD"

linuxdeploy --appdir AppDir \
            --plugin qt \
            --output appimage \
            --desktop-file packaging/linux/microscope.desktop \
            --icon-file packaging/linux/microscope.png
echo "Built Microscope-x86_64.AppImage"
