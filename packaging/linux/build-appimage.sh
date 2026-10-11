#!/usr/bin/env bash
# AppImage: chmod +x and run. No package manager, no sudo (spec 12).
#
# PARTIALLY VERIFIED (2026-10-10, Arch Linux / Qt 6.8). This script now runs
# end to end and produces Microscope-x86_64.AppImage (~100 MB). The AppImage
# starts, loads its QML, and builds its window.
#
# IT IS NOT YET SHIPPABLE, for two separate reasons.
#
# 1. It segfaults in the dynamic loader while dlopen-ing the Qt Multimedia
#    ffmpeg backend (reached from VideoOutput in ui/VideoView.qml -- the
#    moment it tries to show video). Isolated to the five bundled ffmpeg
#    libraries: drop them and it runs. Do NOT "fix" that with
#    --exclude-library here; see docs/manual-test-matrix.md for why that
#    makes field behaviour worse, not better.
#
# 2. Built on this Arch host it inherits glibc 2.44 as a floor, so it will
#    not start on any older distro -- Ubuntu 24.04 LTS has 2.39. An AppImage
#    is supposed to be built against the oldest target base; doing that in a
#    container would address both problems at once.
set -euo pipefail
# Defaults to its own tree, not `build`. The configure step below passes
# -DCMAKE_INSTALL_PREFIX=/usr, which rewrites the cache of whichever tree it
# is handed. Pointed at a developer's `build/`, it silently repoints their
# plain `cmake --install build` at /usr -- a path that needs sudo and so
# contradicts the zero-admin promise this script exists to keep (spec 12).
# A packaging build and a dev build are different configurations and belong
# in different trees. `build-*/` is already gitignored. Pass $1 to override.
BUILD=${1:-build-appimage}

# `linuxdeploy --icon-file` below needs a real 256x256 PNG. One now ships in
# this repo, generated from microscope.svg (the editable master -- edit that,
# not the PNG, and regenerate with the rsvg-convert line in its header). The
# guard stays: the PNG is a build input like any other, and failing here with
# a clear message beats letting `cmake --install` and `linuxdeploy` run first
# and die on an obscure "file not found" partway through.
ICON=packaging/linux/microscope.png
if [[ ! -f "$ICON" ]]; then
  echo "ERROR: $ICON is missing." >&2
  echo "Add a 256x256 PNG app icon there before running this script" \
       "(see docs/manual-test-matrix.md, Known hardware facts)." >&2
  exit 1
fi

# Qt's plugin directory, used further down to stage the Wayland plugins.
# Resolved before anything is built so a Qt-less PATH fails here, loudly,
# rather than half way through a ten-minute deploy. qmake6/qtpaths6 are the
# Arch names; the official Qt binaries the container installs ship the
# unsuffixed ones, so try both rather than assuming either.
qt_plugin_dir=
for q in qmake6 qmake qtpaths6 qtpaths; do
  if command -v "$q" >/dev/null 2>&1; then
    qt_plugin_dir=$("$q" -query QT_INSTALL_PLUGINS 2>/dev/null) && [[ -n $qt_plugin_dir ]] && break
    qt_plugin_dir=
  fi
done
if [[ -z $qt_plugin_dir || ! -d $qt_plugin_dir ]]; then
  echo "ERROR: could not locate Qt's plugin directory." >&2
  echo "Put a Qt 6 qmake6/qtpaths6 on PATH and re-run." >&2
  exit 1
fi

# Start from a clean AppDir. linuxdeploy-plugin-qt decides which Qt modules
# to deploy by inspecting what is already IN the AppDir, so a leftover one
# feeds it the previous run's output and it discovers modules the app never
# asked for -- re-running over a populated AppDir picks up Qt Sql and then
# dies on `Could not find dependency: libfbclient.so.2`. A packaging build is
# cheap to redo and must be reproducible; stale input is worth more than the
# minute it saves. (AppDir/ is gitignored.)
rm -rf AppDir

cmake -B "$BUILD" -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr
cmake --build "$BUILD"
# src/CMakeLists.txt's install(TARGETS microscope ...) is what makes this
# populate AppDir at all -- `cmake --install` with no install() rules exits
# 0 and installs nothing, leaving linuxdeploy with no executable to package.
DESTDIR=AppDir cmake --install "$BUILD"

# Both of these were found by actually running this script on Arch (2026-10-10);
# without them it fails, or worse, succeeds and ships something broken.
#
# NO_STRIP: linuxdeploy bundles its own binutils `strip`, which is older than
# the toolchain that built these binaries and cannot parse the `.relr.dyn`
# section modern linkers emit -- "unknown type [0x13]", and the qt plugin
# aborts. Overridable, in case a future linuxdeploy fixes its strip.
export NO_STRIP=${NO_STRIP:-1}
#
# QML_SOURCES_PATHS: without it linuxdeploy-plugin-qt has nothing to scan for
# QML imports and bundles NO QML modules at all. The build still succeeds and
# the AppImage still launches -- then dies with `module "QtQuick.Controls" is
# not installed`, because this app's own QML is compiled into the binary as
# qrc: while Qt's modules are not. A silent, ship-it-and-find-out failure.
export QML_SOURCES_PATHS=${QML_SOURCES_PATHS:-$PWD/src}

# Deploy in two passes, because the Wayland plugins have to be staged in
# between. Pass 1 is the Qt plugin; the AppImage is only packed in pass 2,
# after the staging below.
linuxdeploy --appdir AppDir \
            --plugin qt \
            --desktop-file packaging/linux/microscope.desktop \
            --icon-file "$ICON"

# ---------------------------------------------------------------------------
# Stage the Wayland QPA plugin.
#
# linuxdeploy-plugin-qt gets this half right on its own and the half it misses
# is fatal. It detects `waylandclient` among the Qt modules and duly bundles
# libQt6WaylandClient.so.6 -- but it never deploys platforms/libqwayland*.so,
# the QPA plugin that library exists to serve, nor the wayland-* helper
# plugins. The result passes every build check and then aborts on startup on
# any Wayland session, because Qt treats "the platform plugin I was asked for
# is not here" as qFatal():
#
#   Could not find the Qt platform plugin "wayland" in ""
#   This application failed to start because no Qt platform plugin could be
#   initialized.  Available platform plugins are: xcb.
#
# That abort is what produced the SIGABRT core dumps of 2026-10-10 20:57.
# It only bites when QT_QPA_PLATFORM names wayland alone; a desktop that sets
# the usual `wayland;xcb` fallback list (Omarchy does) silently lands on xcb
# and runs under XWayland instead, which is exactly why this shipped unnoticed.
#
# The helper plugins are not optional extras: without wayland-shell-integration
# the plugin loads and then cannot create a window at all.
# wayland-graphics-integration-server and the compositor plugins are for apps
# that ARE compositors, so they stay out.
#
# Naming differs by vendor -- Arch merges the QPA plugin into libqwayland.so
# where the official Qt binaries split it into libqwayland-generic.so and
# libqwayland-egl.so -- hence the glob rather than a fixed filename.
# ---------------------------------------------------------------------------
wayland_plugin_dirs=(
  wayland-shell-integration
  wayland-graphics-integration-client
  wayland-decoration-client
)

shopt -s nullglob
wayland_qpa=("$qt_plugin_dir"/platforms/libqwayland*.so)
shopt -u nullglob
if (( ${#wayland_qpa[@]} == 0 )); then
  echo "ERROR: no Wayland QPA plugin in $qt_plugin_dir/platforms." >&2
  echo "Install Qt's Wayland component (Arch: qt6-wayland; aqtinstall: -m qtwayland)" \
       "and re-run. Packaging without it produces an AppImage that aborts on" \
       "startup on a Wayland-only session, so this is a hard failure rather" \
       "than a warning." >&2
  exit 1
fi

# --deploy-deps-only is the point of the two passes: the files are already in
# the AppDir, so linuxdeploy must resolve THEIR dependencies (libQt6WlShell-
# Integration.so.6 and friends) and rewrite their rpath without copying them
# again. Without it they keep the host's $ORIGIN/../../../ rpath, which from
# AppDir/usr/plugins/platforms/ points at AppDir/usr/ and finds nothing.
deps_only=()
mkdir -p AppDir/usr/plugins/platforms
for so in "${wayland_qpa[@]}"; do
  cp -a "$so" AppDir/usr/plugins/platforms/
  deps_only+=("--deploy-deps-only=AppDir/usr/plugins/platforms/$(basename "$so")")
done
for d in "${wayland_plugin_dirs[@]}"; do
  [[ -d "$qt_plugin_dir/$d" ]] || continue
  mkdir -p "AppDir/usr/plugins/$d"
  shopt -s nullglob
  for so in "$qt_plugin_dir/$d"/*.so; do
    cp -a "$so" "AppDir/usr/plugins/$d/"
    deps_only+=("--deploy-deps-only=AppDir/usr/plugins/$d/$(basename "$so")")
  done
  shopt -u nullglob
done

linuxdeploy --appdir AppDir \
            "${deps_only[@]}" \
            --output appimage \
            --desktop-file packaging/linux/microscope.desktop \
            --icon-file "$ICON"

# Assert what the comment above says, so a future linuxdeploy that starts
# deploying this itself, or a Qt install that quietly lacks it, cannot take
# the plugin back out again without failing the build.
shopt -s nullglob
packed=(AppDir/usr/plugins/platforms/libqwayland*.so)
shopt -u nullglob
if (( ${#packed[@]} == 0 )); then
  echo "ERROR: the Wayland QPA plugin is not in the finished AppDir." >&2
  exit 1
fi
echo "Built Microscope-x86_64.AppImage"
echo "  platform plugins: $(cd AppDir/usr/plugins/platforms && echo *.so)"
