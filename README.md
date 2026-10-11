# Microscope

A field-inspection tool for USB UVC microscopes. A technician connects a
USB-C microscope to a laptop, inspects a sample on a live view, and captures
still images and video as evidence — fully offline, with no IT involvement
needed to install or run it (see `docs/superpowers/specs/2026-09-30-microscope-app-design.md`
for the full design).

## Features

1. **Live view** of a connected USB UVC microscope.
2. **Digital zoom** in/out (1x-8x) with pan — wheel/mouse or pinch on a
   touchscreen, centred on the cursor or pinch centroid, with a reset-to-fit
   control and pan clamped to the frame edges.
3. **Snapshot** to a JPEG file, always at full sensor resolution regardless
   of the current zoom/pan — zoom is view-only and never touches the saved
   data.
4. **Video recording** to H.264/MP4, with no audio track. A detach or
   stream stall mid-recording finalizes and names the saved file rather than
   losing the take.

## Supported platforms

| Platform | Status |
|---|---|
| Windows | Supported — built and tested |
| macOS | Supported — **unverified**: never built or tested, see Packaging below |
| Linux | Supported — **unverified**: never built or tested, see Packaging below |
| Android | **Excluded** |
| iOS | **Excluded** |

Phones and tablets are permanently out of scope: iOS has no API for reading
an external USB camera, and Android's path to one is unreliable enough
(external UVC support varies by OEM) that a reliable version would have
needed a native `libuvc`/`libusb` bridge and its own encoder — roughly 60%
of total project effort for a mobile target that was dropped by decision.
See spec §3.1 for the full rationale. Touch *input* is still supported
(pinch-zoom), since a Windows 2-in-1 laptop remains plausible field
hardware even though no mobile OS is targeted.

## Building

Requires Qt 6.8+ (Core, Gui, Quick, Multimedia, Qml, Test), CMake 3.21+, and
a C++17 compiler.

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

On Windows, this project builds with **Qt's MinGW kit**, not MSVC — see
`.superpowers/sdd/2026-09-30-microscope-desktop-app/toolchain-env.md` for
the exact environment (PATH, `CMAKE_GENERATOR=Ninja`, `CMAKE_PREFIX_PATH`)
needed if MSVC isn't available on your machine.

### Packaging zero-admin artifacts

Each platform has its own build-and-package script under `packaging/`; none
require elevation to produce or to run the result (spec §12):

```bash
packaging/windows/build-portable.sh   # -> dist/microscope-windows.zip
packaging/macos/build-app.sh          # -> <build>/microscope.app (sign + notarize before distributing)
packaging/linux/build-appimage.sh     # -> Microscope-x86_64.AppImage
```

The Windows script has been built and run end-to-end on this project's
MinGW toolchain, including confirming the MinGW runtime (`libstdc++-6.dll`,
`libgcc_s_seh-1.dll`, `libwinpthread-1.dll`) is bundled so the portable
folder runs on a machine with no MinGW or Qt installed.

The **macOS** script is written to spec but **unverified** — this
development machine has no macOS toolchain. Run it for the first time with
extra scrutiny, and update `docs/manual-test-matrix.md` with the results.

The **Linux** script now **runs end to end and produces
`Microscope-x86_64.AppImage` (~96 MB), but that AppImage is not yet
shippable.** Two independent blockers:

1. It starts and loads its QML, then segfaults in the dynamic loader while
   `dlopen`-ing the Qt Multimedia ffmpeg backend — it dies at the moment it
   would show video. Isolated to the five bundled ffmpeg libraries; drop
   them and it runs clean.
2. Built on an Arch host it inherits **glibc 2.44** as a floor, so it will
   not start on any older distro (Ubuntu 24.04 LTS ships 2.39).

Building inside an old-base container is the standard AppImage practice and
addresses both at once:

```bash
packaging/linux/build-appimage-docker.sh    # -> Microscope-x86_64.AppImage
```

That path (`packaging/linux/Dockerfile.appimage`) builds on Ubuntu 22.04
(glibc 2.35) with the official Qt 6.8 binaries, which ship their own FFmpeg
rather than bundling the host's. It reuses `build-appimage.sh` inside the
container so both paths share the same fixes. **It has been written and its
inputs verified — Qt 6.8.3 / `linux_gcc_64` / the `qtmultimedia`,
`qtshadertools`, `qtimageformats` modules all resolve against Qt's servers,
and `qtdeclarative` plus `qtwayland` are in the base install — but it has
never been run**, because the Docker daemon on this machine is inactive and
needs `sudo systemctl start docker` first. After that it can be run either
unprivileged (requires `sudo usermod -aG docker "$USER"` and a re-login) or,
without that permanent group grant, as
`DOCKER="sudo docker" packaging/linux/build-appimage-docker.sh`. Two other defects found on the first real run are already fixed
in the script (`NO_STRIP=1` for a `.relr.dyn` strip failure, and
`QML_SOURCES_PATHS`, without which *no* QML modules were bundled at all).
The script also stages Qt's Wayland QPA plugin and its shell-integration
helpers itself, in a second pass — `linuxdeploy-plugin-qt` bundles
`libQt6WaylandClient.so.6` but not the `platforms/libqwayland*.so` that
library serves, so without this the AppImage aborted on a Wayland-only
session. The script needs `linuxdeploy` and
`linuxdeploy-plugin-qt` on PATH — upstream release AppImages in
`~/.local/bin` work and need no sudo. See `docs/manual-test-matrix.md`.

The app icon lives at `packaging/linux/microscope.svg` (editable master) and
`packaging/linux/microscope.png` (256x256, generated). Edit the SVG and
regenerate with the `rsvg-convert` line in its header; never edit the PNG by
hand.

## Running tests

```bash
ctest --test-dir build --output-on-failure
```

All hardware-independent behaviour — zoom clamping, filename collisions,
disk-space thresholds, format-preference ordering, and every row of the
spec's failure table (detach mid-recording, stall, exhausted format chain,
etc.) — is covered by automated tests running against `FakeCaptureSource`,
with no physical scope attached. What can't be simulated honestly — real
USB enumeration, real isochronous bandwidth on a shared hub, real
touchscreen pinch gestures — is covered instead by
`docs/manual-test-matrix.md`, which must be run against real hardware on
all three platforms before every release.

## Where captures are written

Snapshots and recordings are written to a `Microscope` folder under the
platform's standard Movies location (falling back to Documents, then the
home directory, if Movies isn't available) — e.g. `~/Videos/Microscope` on
Windows, `~/Movies/Microscope` on macOS. Filenames are timestamped, with a
numeric suffix on same-second collisions so a double-tap can't overwrite a
prior capture. The "Open output folder" control in the app opens this
location directly.
