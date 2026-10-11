#!/usr/bin/env bash
# Build the AppImage inside an old-base container instead of on this host.
# This is the path that can actually produce a shippable artifact; running
# packaging/linux/build-appimage.sh directly on a rolling-release host gives
# you an AppImage that only runs on a host as new as the one that built it
# (see the header of that script, and docs/manual-test-matrix.md).
#
# Deliberately reuses build-appimage.sh inside the container rather than
# duplicating it, so the NO_STRIP and QML_SOURCES_PATHS fixes apply to both
# paths and cannot drift apart.
set -euo pipefail

QT_VERSION=${QT_VERSION:-6.8.3}
UBUNTU_VERSION=${UBUNTU_VERSION:-22.04}
IMAGE=${IMAGE:-microscope-appimage-builder}

# Overridable so you can use `sudo docker` without joining the docker group.
# Joining that group is a permanent, effectively root-equivalent grant; a
# one-off sudo is the smaller commitment for a build you may run rarely:
#
#   DOCKER="sudo docker" packaging/linux/build-appimage-docker.sh
#
# Invoke it that way, NOT as `sudo packaging/linux/...` -- the --user flag
# below resolves your real uid, and running the whole script under sudo makes
# that root and leaves root-owned build output in your working tree.
DOCKER=${DOCKER:-docker}

if ! $DOCKER info >/dev/null 2>&1; then
  echo "ERROR: cannot talk to the Docker daemon (using: $DOCKER)." >&2
  echo "  start it:          sudo systemctl start docker" >&2
  echo "  then EITHER run:   DOCKER=\"sudo docker\" \$0" >&2
  echo "  or grant access:   sudo usermod -aG docker \"\$USER\"  (then log out and back in)" >&2
  exit 1
fi

echo "==> building image $IMAGE (Ubuntu $UBUNTU_VERSION, Qt $QT_VERSION)"
$DOCKER build \
  -f packaging/linux/Dockerfile.appimage \
  --build-arg QT_VERSION="$QT_VERSION" \
  --build-arg UBUNTU_VERSION="$UBUNTU_VERSION" \
  -t "$IMAGE" \
  packaging/linux

# --user keeps build output owned by you rather than root. HOME is set
# because a uid with no /etc/passwd entry otherwise has none, and parts of
# the Qt/CMake toolchain write into it.
echo "==> building the AppImage in the container"
$DOCKER run --rm \
  --user "$(id -u):$(id -g)" \
  -e HOME=/tmp \
  -v "$PWD":/src \
  -w /src \
  "$IMAGE" \
  bash packaging/linux/build-appimage.sh build-appimage-docker

echo "==> done: Microscope-x86_64.AppImage"
