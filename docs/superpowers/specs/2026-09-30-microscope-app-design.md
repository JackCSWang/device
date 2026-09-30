# Cross-Platform Microscope Capture App — Design

**Date:** 2026-09-30
**Status:** Approved design, pending implementation plan

## 1. Purpose

A field-inspection tool. A technician carries a USB-C UVC microscope to a
site, connects it to an Android tablet or a laptop, inspects a sample on a
live view, and captures still images and video as evidence.

Priorities, in order: portability, offline operation, and not needing IT
involvement to install or run.

## 2. Scope

**In scope (v1)**

1. Live view of a connected USB UVC microscope
2. Digital zoom in/out, with pan
3. Snapshot to a still image file
4. Video recording to a file

**Out of scope (v1)** — deferred, not cancelled:

- Job/session tagging and grouping of captures
- On-image measurement and scope calibration
- Export, report generation, or sync to a server
- Audio track on recordings
- Multiple simultaneous scopes
- Login, user accounts, or any network dependency

## 3. Platform targets

| Platform | Status |
|---|---|
| Windows | Supported |
| macOS | Supported |
| Linux | Supported |
| Android | Supported |
| iOS | **Excluded** |

### 3.1 Why iOS is excluded

iOS provides no API for reading external USB cameras. iPadOS 17+ can open
UVC devices over USB-C, but iPhone cannot, at any price. Since the scope is
a USB-C UVC device, "runs on iPhone" and "reads this microscope" are
mutually exclusive.

This was raised and accepted deliberately. Android replaced iOS as the
mobile target. Revisiting iOS would require either a Wi-Fi microscope or
turning the phone into a remote viewer for a desktop that holds the device
— both are new projects, not variations of this one.

## 4. Non-functional requirements

1. **No elevated privileges.** The app installs and runs without admin,
   root, or sudo on all four platforms. No kernel driver, no system
   service, no elevated helper process, ever.
2. **Fully offline.** No network call is required for any feature.
3. **No admin-installed dependencies.** Everything ships in the bundle.

Requirement 1 is architectural, not packaging trivia. It is cheap to honour
now and expensive to retrofit; any future feature that needs a driver or a
service violates the product premise.

**Known exception:** on a locked-down Linux install the user may lack access
to `/dev/video*`, whose one-time fix (`usermod -aG video`) needs root. The
app must detect this and display the exact command rather than failing
opaquely. See 10.3.

## 5. Technology choices

| Decision | Choice | Rationale |
|---|---|---|
| Framework | Qt 6 | Only option giving capture *and* H.264 recording on all 3 desktop platforms from one API, with no native code |
| Language | C++17 | Forced: PySide6 has no viable Android deployment path |
| UI toolkit | Qt Quick / QML | Forced: Qt Widgets is poor on touch, and pinch-to-zoom is the core interaction |
| Licence | LGPLv3 | Acceptable for internal use. `androiddeployqt` bundles Qt as shared libraries, so LGPL is satisfied. **Revisit if this becomes a sold product** — static linking on Android would push toward a commercial licence |

### 5.1 Approaches considered and rejected

- **Flutter + native media core.** Nicer Android UI, but requires writing
  Media Foundation, AVFoundation, and V4L2 capture *plus* a desktop
  encoding stack by hand. Large cost for a four-feature app.
- **Web stack (Tauri/Electron) + separate native Android app.** Fastest
  desktop route via `getUserMedia`, but Chrome on Android does not expose
  external UVC cameras, so Android needs a full separate implementation.
  Two codebases is a bad trade here.
- **libuvc on every platform.** Not viable. Windows and macOS bind UVC
  devices to their in-box class driver; libusb would require detaching it
  and installing a replacement driver, which breaks requirement 4.1.

## 6. The dominant risk

Reading a UVC camera is nearly free on desktop and genuinely hard on
Android.

Windows, macOS, and Linux each ship a UVC class driver and expose the
device through a standard capture API. Android does not. Its `camera2` API
enumerates external USB cameras only on some devices, at each OEM's
discretion, which is not shippable. The reliable route is claiming the
device through the USB Host API and decoding the stream in native code via
`libuvc`/`libusb` behind a JNI bridge.

**Android is roughly 60% of total effort and nearly all technical risk.**
Any plan treating the four platforms as equal work is wrong.

**This must be de-risked first.** Prove frame acquisition from the actual
scope on an actual target tablet before building UI on top of it. It is the
one assumption capable of invalidating the whole schedule.

## 7. Architecture

The system turns on one seam: **two capture backends behind a single
interface.**

```
        +----------- Desktop -----------+   +------- Android -------+
        | QCamera + QMediaCaptureSession|   | USB Host API (JNI)    |
        | Win: MF  mac: AVF  Linux: V4L2|   | libuvc / libusb (NDK) |
        +---------------+---------------+   +-----------+-----------+
                        |                               |
                 +------v-------------------------------v------+
                 |            ICaptureSource                   |
                 |     frameReady(QVideoFrame) -- full res     |
                 +------+-------------------------------+------+
                        |                               |
            +-----------v----------+       +------------v-----------+
            | view                 |       | output                 |
            | zoom + pan           |       | snapshot writer        |
            | -> QML VideoOutput   |       | recorder               |
            |                      |       |                        |
            | NEVER writes files   |       | NEVER sees zoom state  |
            +----------------------+       +------------------------+
```

### 7.1 Modules

| Module | Responsibility | Depends on |
|---|---|---|
| `device` | Enumerate scopes, watch attach/detach, hold permissions | platform APIs |
| `capture` | `ICaptureSource`; `QtCaptureSource`, `UvcCaptureSource` | `device` |
| `view` | Zoom/pan transform, render to QML | nothing — consumes frames |
| `output` | JPEG snapshot writer, MP4 recorder | nothing — consumes frames |
| `storage` | Output directory, timestamped naming, free-space checks | platform dirs |
| `ui` | QML shell wiring the above | all |

### 7.2 Two structural properties

**Zoom semantics are enforced by structure.** `view` and `output` both
receive the identical full-resolution frame and cannot reach each other.
`output` has no path by which to learn the zoom level, so snapshots are
physically incapable of being cropped by accident. The decision cannot rot
through later edits.

**`view` and `output` do not know cameras exist.** They consume frames.
This is what makes section 11 possible.

### 7.3 Platform-specific surface

After the refinement in 8.1, only two things differ per platform:

1. Frame acquisition
2. Video encoding

Everything else, snapshots included, is shared code.

## 8. Data flow and threading

### 8.1 Snapshots are platform-independent

A snapshot is: current `QVideoFrame` -> `QImage` -> JPEG. Pure Qt,
identical on every platform.

`QImageCapture` is deliberately **not** used on desktop. It can switch the
device into a separate still-image pipeline; the scope has exactly one
stream, so grabbing the live frame is both more deterministic and yields
one implementation instead of two.

### 8.2 Threads

| Thread | Owns | Rule |
|---|---|---|
| Capture | USB device, format negotiation | Never blocks on disk or UI |
| UI | QML render, zoom transform | Only reads the latest frame |
| Encoder | `QMediaRecorder` (desktop, internal) / `MediaCodec` (Android, JNI) | Fed from capture thread |
| Writer pool | JPEG encode + file write | Short-lived `QThreadPool` tasks |

JPEG-encoding a full-resolution frame takes tens of milliseconds. On the UI
thread that is a visible stutter on every snapshot; on the capture thread it
drops frames. Hence the pool.

### 8.3 Frame ownership rule

`QVideoFrame` is reference-counted, but **the underlying buffer belongs to
the capture backend and is recycled.** Therefore anything leaving the
capture thread's stack must be deep-copied first: map the frame, clone into
a `QImage`, then hand that off.

This is a spec-level rule, not a code-review aspiration. Violating it
produces snapshots that are half one frame and half the next,
intermittently, under load only.

### 8.4 Fan-out and backpressure

```
capture thread --> QVideoFrame (+ capture timestamp)
                        |
        +---------------+---------------+
        v                               v
   view (UI thread)               output (when armed)
   latest-frame-wins              +- snapshot: deep copy -> pool
   drops freely, no queue         +- recorder: fed with PTS
```

**Display path drops stale frames deliberately.** An unbounded queue on a
slow tablet becomes latency, then a crash.

**Recording path is timestamp-driven.** Every frame carries its capture
timestamp, used as the encoder PTS; video is written variable-frame-rate.
If the encoder falls behind on weak hardware the result is a lower frame
rate that still plays at correct wall-clock speed, rather than a file that
plays fast, which for inspection evidence would be actively misleading.

### 8.5 Sequence

1. `device` enumerates; auto-open if exactly one scope, otherwise prompt
2. Negotiate format down a preference list: MJPEG 1080p -> YUY2 720p ->
   first supported
3. Frames flow; `view` renders; `output` idle
4. **Snapshot:** arm flag -> next frame deep-copied -> writer pool -> JPEG
   -> UI confirms the filename
5. **Record:** open encoder -> feed frames with PTS -> stop -> finalize
6. **Detach:** stop capture, finalize any recording, return to "connect a
   scope"

## 9. Zoom

Zoom is **view-only**. Every snapshot and every recording contains the full
sensor frame at full resolution, regardless of zoom or pan state.

Implementation is a pure UI-side transform on the `VideoOutput` item. No
frame data is touched.

- Range 1x to 8x digital; beyond that it is only blur
- Centred on the pinch centroid (touch) or mouse cursor (desktop wheel)
- Pan clamped so the viewport never leaves the frame
- **Reset-to-fit control required.** It is easy to get lost at 8x on a
  bench, and hunting for the sample is the kind of friction that makes a
  tool unpopular
- Bilinear filtering by default

## 10. Failure handling

Every error message must state what happened, whether data was saved, and
the one action to take. Generic failures are what generate support calls
from the field, where nobody can read a log.

### 10.1 Detach mid-recording

The defining field failure: a technician leans over and the cable pops out
while recording.

**MP4 requires its `moov` atom, written at the end. A truncated MP4 is
unplayable — the evidence is simply gone.** Therefore on detach the recorder
receives an explicit finalize: signal EOS, drain the encoder, close the
muxer. Only then does the UI report `recording stopped, saved to <filename>`.

Naming the saved file is required, not decorative. "Device disconnected"
alone leaves the technician assuming the take was lost.

**Accepted exposure:** this covers cable snags and scope failures, because
the app survives. It does *not* cover app crash or battery death, which
truncate the file mid-write. The fix would be fragmented MP4 or Matroska,
both of which survive truncation, but `QMediaRecorder` does not expose muxer
flags, so it would mean bypassing Qt's recorder on desktop as well. v1
accepts the exposure. Revisit if dead batteries prove common in practice.

### 10.2 USB bandwidth

UVC uses isochronous transfers with reserved bandwidth, so the classic
failure on a shared hub is: the device opens successfully and then no frames
ever arrive. No error, just black. This is live for the known setup, where
the scope sits behind a 6-in-1 multiport hub alongside a card reader.

**A successful `open()` is not evidence that streaming works.** Hence a
frame-arrival watchdog: no first frame within ~3s of open -> drop to the
next-lower format and retry -> if that also fails, report "no video
received; try a direct port instead of the hub, or a lower resolution."

### 10.3 Failure table

| Failure | Response |
|---|---|
| Detach mid-recording | Finalize file, name it in UI, return to idle |
| Detach mid-snapshot | Discard partial write; no orphan file |
| No frames after open | Watchdog -> downgrade format -> retry -> actionable message |
| Stream stalls later (no frame for 5s) | One silent reopen attempt, then surface |
| Linux `EACCES` on `/dev/video*` | Show the literal fix: `sudo usermod -aG video $USER`, then re-login |
| macOS camera denied | Deep-link to Privacy & Security -> Camera |
| Android USB permission denied | Explain, offer retry; `device_filter.xml` prevents re-asking on every attach |
| Device claimed by another app | Name the conflicting app, not "failed to open" |
| Disk nearly full | Refuse to start recording below 500 MB free; during recording, stop and finalize at 100 MB free. Refuse snapshot below 50 MB |
| All formats fail | Report what the scope *advertised*, for diagnosability |
| Android backgrounded | Stop and finalize, notify. No foreground service in v1 |

### 10.4 Accepted limitation

Filenames use device local time, and offline tablets drift. A wrong clock
means wrong evidence timestamps. Known; not solved in v1.

## 11. Testing

TDD applies. `view` and `storage` are pure logic and are written test-first.

| Layer | Coverage |
|---|---|
| Unit | Zoom clamping, pan bounds, reset-to-fit, filename generation, free-space math, format-preference ordering — pure functions, no hardware |
| Component | `FakeCaptureSource` drives the real pipeline: snapshot a known pattern and assert pixels; record 5s and assert the MP4 is valid and ~5s long |
| Failure injection | `FakeCaptureSource` can detach mid-recording, stall, or emit zero frames — every row of 10.3 gets an automated test **with no scope attached** |
| Manual | Real scope on all four platforms, via a direct port *and* through the hub |

`FakeCaptureSource` implements `ICaptureSource` and emits a synthetic moving
test pattern. Because it sits at the same seam as the real backends, the
entire app — UI, zoom, snapshot, recording — runs and is testable on CI with
no microscope attached. Without this seam, testing means hand-verification
with hardware on four machines, which in practice means not testing.

Detach-mid-recording is simultaneously the most important behaviour in the
app and the most tedious to verify by hand. Behind a fake source it is a CI
test.

Hardware verification stays manual. There is no honest way to test real USB
bandwidth negotiation without real USB.

## 12. Packaging

All four artifacts install without admin:

| Platform | Artifact | Notes |
|---|---|---|
| Windows | Portable folder / ZIP via `windeployqt` | Unzip and run. Optional per-user installer to `%LOCALAPPDATA%` |
| macOS | `.app` via `macdeployqt`, drag to `~/Applications` | **Requires Developer ID signing + notarization (~$99/yr)** or Gatekeeper blocks it outright |
| Linux | AppImage | `chmod +x` and run |
| Android | APK via `androiddeployqt` | Sideload or MDM push |

**Code signing is the real gate, not privilege.** Unsigned Windows binaries
trigger SmartScreen until reputation accrues; unsigned macOS apps are
blocked entirely. Budget for certificates.

**Confirm before Android rollout:** MDM policy may forbid installation from
outside the Play Store. This is an organisational dependency, not a
technical one.

## 13. Output files

- Snapshots: JPEG, full sensor resolution
- Video: H.264 in MP4, no audio track
- Destination: a plain user-visible folder, timestamped filenames
- No database, no index, no metadata sidecar

## 14. Open questions

1. **Audio on recordings.** Assumed absent. If technicians want spoken
   notes, this changes the recording pipeline on both platforms.
2. **High-zoom filtering.** Bilinear by default. Nearest-neighbour shows
   actual sensor pixels and is arguably more honest for evidence work.
3. **Crash/power-loss resilience for video.** See 10.1.
4. **Commercial Qt licence.** Only if this becomes a sold product. See 5.
