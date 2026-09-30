#!/usr/bin/env bash
# Portable folder -- unzip and run, no installer, no admin (spec 12).
#
# This build uses Qt's MinGW kit (see toolchain-env.md), not MSVC. windeployqt
# does not bundle the MinGW C/C++ runtime by default -- only the Qt DLLs -- so
# without --compiler-runtime the portable folder runs fine on THIS machine
# (which has the MinGW toolchain on PATH) but fails to start on a clean
# machine with "libstdc++-6.dll not found", defeating the whole point of a
# zero-admin portable build. --compiler-runtime tells windeployqt to also copy
# libstdc++-6.dll, libgcc_s_seh-1.dll and libwinpthread-1.dll from the MinGW
# bin directory that is currently on PATH.
set -euo pipefail
BUILD=${1:-build}
OUT=dist/microscope-windows

cmake -B "$BUILD" -DCMAKE_BUILD_TYPE=Release
cmake --build "$BUILD" --config Release

rm -rf "$OUT" && mkdir -p "$OUT"
# qt_standard_project_setup() (root CMakeLists.txt) points
# CMAKE_RUNTIME_OUTPUT_DIRECTORY at the top of the build tree, so the
# executable lands at $BUILD/microscope.exe, not $BUILD/src/microscope.exe
# as in a per-target-directory layout.
cp "$BUILD/microscope.exe" "$OUT/"
windeployqt --qmldir src/ui --release --compiler-runtime "$OUT/microscope.exe"

# Neither Info-ZIP's `zip` nor 7-Zip is a stock part of a Windows install, and
# the whole point of this script is zero-admin, nothing-preinstalled
# packaging -- so don't require either. Bash's own `zip` is preferred when
# present (e.g. on a CI image that has it); otherwise fall back to
# C:\Windows\System32\tar.exe, which has shipped built into Windows since the
# 1803 update. That one is libarchive's bsdtar and writes a real,
# standard-conforming .zip via `-a` (auto-detected from the .zip extension).
# Git for Windows also installs a `tar` earlier on PATH in git-bash, but
# that's GNU tar, which has no zip support and would silently produce a
# mislabeled .tar -- so the System32 copy must be named explicitly, not
# found via plain `tar`.
( cd dist && rm -f microscope-windows.zip
  if command -v zip >/dev/null 2>&1; then
    zip -qr microscope-windows.zip microscope-windows
  else
    /c/Windows/System32/tar.exe -a -c -f microscope-windows.zip microscope-windows
  fi )
echo "Built dist/microscope-windows.zip"
