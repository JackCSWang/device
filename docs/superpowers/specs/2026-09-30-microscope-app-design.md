# Cross-Platform Microscope Capture App — Design

**Date:** 2026-09-30
**Status:** Approved design
**Revision 2 (2026-09-30):** Android dropped. Targets are Windows, macOS, and
Linux only. This removed the project's dominant technical risk; §5, §6, §7,
§10, and §12 changed materially. Revision 1 history is preserved in §16.
**Revision 3 (2026-10-02):** View controls added. A snapshot keyboard shortcut
(Space), on-screen zoom buttons, and mirror/rotation. Orientation is the first
transform that IS applied to saved files; §9 now distinguishes it from zoom
and says why.

## 1. Purpose

A field-inspection tool. A technician carries a USB-C UVC microscope to a
site, connects it to a laptop, inspects a sample on a live view, and captures
still images and video as evidence.

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

**Out of scope permanently:** phones and tablets. See §3.

## 3. Platform targets

| Platform | Status |
|---|---|
| Windows | Supported |
| macOS | Supported |
| Linux | Supported |
| Android | **Excluded** |
| iOS | **Excluded** |

### 3.1 Why there is no mobile version

**iOS** provides no API for reading external USB cameras. iPadOS 17+ can open
UVC devices over USB-C, but iPhone cannot, at any price. Since the scope is a
USB-C UVC device, "runs on iPhone" and "reads this microscope" are mutually
exclusive.

**Android** was the mobile target in revision 1 and was dropped by decision,
not by technical obstacle. It was, however, the expensive target: Android
ships no usable path to an external UVC camera, because its `camera2` API
enumerates external USB cameras only on some devices at each OEM's
discretion. The reliable route would have been claiming the device through
the USB Host API and decoding the stream in native code via `libuvc`/`libusb`
behind a JNI bridge, plus a second video encoder built on `MediaCodec`.

That work was estimated at roughly 60% of total project effort and nearly all
of its technical risk. Dropping it is the single largest simplification
available to this project — see §6.

**Consequence for the field story:** captures happen on a laptop, not a
tablet. A Windows 2-in-1 remains plausible field hardware, which is why touch
input is still supported (§9), but no mobile operating system is a target.

## 4. Non-functional requirements

1. **No elevated privileges.** The app installs and runs without admin,
   root, or sudo on all three platforms. No kernel driver, no system service,
   no elevated helper process, ever.
2. **Fully offline.** No network call is required for any feature.
3. **No admin-installed dependencies.** Everything ships in the bundle.

Requirement 1 is architectural, not packaging trivia. It is cheap to honour
now and expensive to retrofit; any future feature that needs a driver or a
service violates the product premise.

**Known exception:** on a locked-down Linux install the user may lack access
to `/dev/video*`, whose one-time fix (`usermod -aG video`) needs root. The
app must detect this and display the exact command rather than failing
opaquely. See §10.3.

## 5. Technology choices

| Decision | Choice | Rationale |
|---|---|---|
| Framework | Qt 6 | One API — `QCamera` plus `QMediaRecorder` — covers capture *and* H.264 recording on all three platforms with no native code |
| Language | C++17 | A choice, not a constraint. See §5.1 |
| UI toolkit | Qt Quick / QML | A choice, not a constraint. See §5.2 |
| Licence | LGPLv3 | Satisfied comfortably: `windeployqt`, `macdeployqt`, and `linuxdeploy` all ship Qt as shared libraries |

### 5.1 C++ is no longer forced

In revision 1, C++ was forced: PySide6 has no viable Android deployment
path. With Android gone, **Python plus PySide6 would now work on all three
targets**, and that should be stated rather than quietly ignored.

C++ is kept anyway, for one reason that survives the change: requirement 4.1
demands a zero-admin portable artifact, and shipping a Python app that way
means PyInstaller or Nuitka, a bundled interpreter, and a noticeably larger
and more fragile bundle. A single native binary with Qt libraries beside it
is the simpler thing to hand someone who cannot run an installer.

This is a preference with a reason, not a requirement. If the team's skills
point at Python, the architecture in §7 transfers unchanged.

### 5.2 QML is no longer forced

In revision 1, QML was forced: Qt Widgets is poor on touch, and the Android
tablet was a primary target. With no mobile target, Widgets became viable.

QML is kept because Qt Quick's input handlers give pinch, wheel, and drag
zoom from one code path, and Windows 2-in-1 devices are plausible field
hardware. The gain over Widgets is modest; the cost of switching now is a
rewrite of the UI task for no functional difference.

### 5.3 Approaches considered and rejected

Revision 1 chose Qt partly because it was the only framework covering all
four platforms including Android in one codebase. **That argument is now
void, so the decision was re-examined rather than inherited.**

- **Web stack (Tauri or Electron) + `getUserMedia`.** This became
  substantially more attractive when Android was dropped: `getUserMedia`,
  `MediaRecorder`, and a canvas would deliver all four features on three
  desktops with essentially no native code. Rejected because this app
  produces evidence, and the web media stack gives away the control that
  matters for it: `MediaRecorder` defaults to WebM/VP8 with patchy and
  platform-dependent H.264/MP4 support, explicit format negotiation
  (§8.5) is not exposed, precise presentation timestamps (§8.4) are not
  controllable, and Tauri's system WebView means WebKitGTK on Linux, where
  codec and capture support varies by distribution. Electron additionally
  bundles roughly 150 MB of Chromium into a tool whose premise is a small
  portable artifact.
- **Flutter + native media core.** Its advantage was a better mobile UI,
  which is now worth nothing. It still requires writing Media Foundation,
  AVFoundation, and V4L2 capture plus a desktop encoding stack by hand.
  Strictly worse than Qt here.
- **libuvc on every platform.** Not viable. Windows and macOS bind UVC
  devices to their in-box class driver; libusb would require detaching it
  and installing a replacement driver, which breaks requirement 4.1.

Qt survives the re-examination on a narrower but sufficient basis: it is the
only option that gives evidence-grade control of format, timestamps, and
container while requiring no native code and no runtime beyond its own
shared libraries.

## 6. Risk profile

Revision 1 named Android as the one assumption capable of invalidating the
schedule, and required a spike before any production code. **That risk is
gone, and with it the spike.** No remaining item in this project can
invalidate its schedule.

What is left, in order of how likely it is to cause trouble:

1. **Code signing and notarization** (§12). An unsigned macOS app is blocked
   by Gatekeeper outright. This is an organisational dependency — an Apple
   Developer account and a Windows certificate — not an engineering problem,
   which means it can block a release while looking like nothing is wrong.
   Start it early.
2. **USB isochronous bandwidth on shared hubs** (§10.2). Technically
   understood and mitigated by design, but it produces a black screen with
   no error, so it costs debugging time whenever it appears.
3. **Qt Multimedia backend variance across Linux distributions.** Qt 6.5+
   defaults to its FFmpeg backend, which makes behaviour far more consistent
   than the old GStreamer path, but Linux remains the platform where a
   distribution can surprise you. The manual test matrix (§11) covers it.

None of these are reasons to delay building. All three are reasons to test on
real hardware early rather than at the end.

## 7. Architecture

```
                 +--------------- Desktop ----------------+
                 |     QCamera + QMediaCaptureSession     |
                 |  Win: MF   macOS: AVF   Linux: V4L2    |
                 +-------------------+--------------------+
                                     |
                 +-------------------v--------------------+
                 |             ICaptureSource             |
                 |    frameReady(QVideoFrame) -- full res |
                 +------+--------------------------+------+
                        |                          |
            +-----------v----------+   +-----------v-----------+
            | view                 |   | output                |
            | zoom + pan           |   | snapshot writer       |
            | -> QML VideoOutput   |   | recorder              |
            |                      |   |                       |
            | NEVER writes files   |   | NEVER sees zoom state |
            +----------------------+   +-----------------------+
```

### 7.1 Modules

| Module | Responsibility | Depends on |
|---|---|---|
| `device` | Enumerate scopes, watch attach/detach | Qt Multimedia |
| `capture` | `ICaptureSource`; `QtCaptureSource`, `FakeCaptureSource` | `device` |
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
This is what makes §11 possible.

### 7.3 Why `ICaptureSource` remains

With one real implementation, the seam is no longer a platform-abstraction
boundary, and dropping it would be a defensible simplification.

It is kept for testability. `FakeCaptureSource` sits at the same seam and
emits a synthetic pattern with failure injection, which is what allows every
row of §10.3 — detach mid-recording included — to be an automated test
instead of someone unplugging a cable on three machines. That justification
is independent of how many platforms exist.

### 7.4 Platform-specific surface: none

Revision 1 had two things differing per platform: frame acquisition and video
encoding. **Both are now handled by Qt on all three targets.** There is no
per-platform code in this design beyond packaging scripts and one Linux
permission hint (§4).

## 8. Data flow and threading

### 8.1 Snapshots

A snapshot is: current `QVideoFrame` → `QImage` → JPEG.

`QImageCapture` is deliberately **not** used. It can switch the device into a
separate still-image pipeline; the scope has exactly one stream, so grabbing
the live frame is more deterministic.

### 8.2 Threads

| Thread | Owns | Rule |
|---|---|---|
| Capture | USB device, format negotiation | Never blocks on disk or UI |
| UI | QML render, zoom transform | Only reads the latest frame |
| Encoder | `QMediaRecorder`, internally | Fed from capture thread |
| Writer pool | JPEG encode + file write | Short-lived `QThreadPool` tasks |

JPEG-encoding a full-resolution frame takes tens of milliseconds. On the UI
thread that is a visible stutter on every snapshot; on the capture thread it
drops frames. Hence the pool.

### 8.3 Frame ownership rule

`QVideoFrame` is reference-counted, but **the underlying buffer belongs to
the capture backend and is recycled.** Therefore anything leaving the capture
thread's stack must be deep-copied first: map the frame, clone into a
`QImage`, then hand that off.

This is a spec-level rule, not a code-review aspiration. Violating it
produces snapshots that are half one frame and half the next, intermittently,
under load only.

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

**Display path drops stale frames deliberately.** An unbounded queue becomes
latency, then a crash.

**Recording path is timestamp-driven.** Every frame carries its capture
timestamp, used as the encoder PTS; video is written variable-frame-rate. If
the encoder falls behind on weak hardware the result is a lower frame rate
that still plays at correct wall-clock speed, rather than a file that plays
fast, which for inspection evidence would be actively misleading.

### 8.5 Sequence

1. `device` enumerates; auto-open if exactly one scope, otherwise prompt
2. Negotiate format down a preference list: MJPEG 1080p → YUY2 720p →
   first supported
3. Frames flow; `view` renders; `output` idle
4. **Snapshot:** arm flag → next frame deep-copied → writer pool → JPEG →
   UI confirms the filename
5. **Record:** open encoder → feed frames with PTS → stop → finalize
6. **Detach:** stop capture, finalize any recording, return to "connect a
   scope"

## 9. Zoom and orientation

### 9.1 Zoom

Zoom is **view-only**. Every snapshot and every recording contains the full
sensor frame at full resolution, regardless of zoom or pan state.

Implementation is a pure UI-side transform on the `VideoOutput` item. No
frame data is touched.

- Range 1x to 8x digital; beyond that it is only blur
- Centred on the mouse cursor, or the pinch centroid on a touchscreen
- Pan clamped so the viewport never leaves the frame. Because the window is
  resizable, the letterboxed case — where the visible region is *larger* than
  the frame on one axis — is reached by ordinary use and must centre rather
  than clamp
- **Reset-to-fit control required.** It is easy to get lost at 8x, and
  hunting for the sample is the kind of friction that makes a tool unpopular
- Bilinear filtering by default
- **On-screen zoom buttons required** (revision 3). Wheel and pinch both
  assume an input the operator may not have: a technician bracing the scope
  against a sample one-handed has no free second finger and often no mouse.
  The buttons zoom about the centre of the viewport, since a button has no
  pointer position to zoom about.

### 9.2 Orientation (revision 3)

Orientation is rotation in 90-degree steps plus an optional horizontal
mirror. Unlike zoom, orientation **is applied to saved snapshots and
recordings** as well as to the live view.

The distinction is not arbitrary. Zoom crops, so baking it in would destroy
the full-sensor-frame guarantee of §7.2. A quarter turn or a mirror is a
pixel permutation: a 640x480 frame rotated 90 degrees is the same 307200
pixels arranged 480x640, nothing resampled and nothing discarded. The
full-frame guarantee therefore survives, and the operator gets evidence
oriented the way they actually observed it instead of sideways files that
someone has to re-rotate in another tool later.

- **Order is fixed: mirror first, then rotate.** The two orders differ for
  every rotation except 180. The live view and the file writers must agree
  exactly, so a single function owns the pixel order and every path calls it
- Quarter turns only. Free-angle rotation would resample (blurring evidence),
  leave empty corners, and serve no inspection purpose
- **The axis swap propagates.** On a quarter turn the recorder is opened at
  the rotated size, and the view's fit-and-pan arithmetic is given the
  rotated frame size. A rotated view computed against the unrotated aspect
  letterboxes against the wrong axis and refuses to pan where picture remains
- **Orientation is locked while a recording is in flight.** The encoder's
  frame size is fixed when the take opens, so a rotation mid-recording would
  feed it frames of the wrong size and produce a truncated or unplayable
  file. The controls grey out, and the capture path independently latches the
  orientation at record start so the corruption stays unreachable even if the
  UI gating is bypassed
- **Orientation survives a pipeline rebuild.** A reopen (device change,
  recovery, replug) constructs a fresh controller, which must inherit the
  current orientation. Silently reverting to upright on reconnect would
  produce evidence inconsistent with everything captured before it, with
  nothing on screen to explain the change
- Changing orientation resets the view to fit, because zoom and pan are
  expressed in the old axes


## 10. Failure handling

Every error message must state what happened, whether data was saved, and
the one action to take. Generic failures are what generate support calls from
the field, where nobody can read a log.

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
the app survives. It does *not* cover app crash or power loss, which truncate
the file mid-write. The fix would be fragmented MP4 or Matroska, both of
which survive truncation, but `QMediaRecorder` does not expose muxer flags,
so it would mean bypassing Qt's recorder entirely. v1 accepts the exposure.

### 10.2 USB bandwidth

UVC uses isochronous transfers with reserved bandwidth, so the classic
failure on a shared hub is: the device opens successfully and then no frames
ever arrive. No error, just black. This is live for the known setup, where
the scope sits behind a 6-in-1 multiport hub alongside a card reader.

**A successful `open()` is not evidence that streaming works.** Hence a
frame-arrival watchdog: no first frame within 3 s of open → **step down the
format preference list and retry** → when the list is exhausted, report "no
video received; try a direct port instead of the hub", naming the formats the
scope advertised.

Complaining without retrying does not satisfy this requirement.

### 10.3 Failure table

| Failure | Response |
|---|---|
| Detach mid-recording | Finalize file, name it in UI, return to idle |
| Detach mid-snapshot | Discard the armed capture; no orphan file |
| No frames after open | Walk down the format chain; on exhaustion, report the advertised formats and suggest a direct port |
| Stream stalls later (no frame for 5 s) | One silent reopen attempt, then surface |
| Linux `EACCES` on `/dev/video*` | Show the literal fix: `sudo usermod -aG video $USER`, then re-login |
| macOS camera denied | Deep-link to Privacy & Security → Camera |
| Windows camera denied | Deep-link to `ms-settings:privacy-webcam` |
| Device claimed by another app | Surface the driver's reported cause; never a bare "failed to open" |
| Disk nearly full | Refuse to start recording below 500 MB free; stop and finalize at 100 MB; refuse snapshot below 50 MB |
| All formats fail | Report what the scope *advertised*, for diagnosability |

### 10.4 Accepted limitation

Filenames use device local time, and offline machines drift. A wrong clock
means wrong evidence timestamps. Known; not solved in v1.

## 11. Testing

TDD applies. `view` and `storage` are pure logic and are written test-first.

| Layer | Coverage |
|---|---|
| Unit | Zoom clamping, letterboxed pan centring, reset-to-fit, filename collisions, free-space thresholds, format-preference ordering — pure functions, no hardware |
| Component | `FakeCaptureSource` drives the real pipeline: snapshot a known pattern and assert pixels; record and assert the MP4 is valid and the right duration |
| Failure injection | `FakeCaptureSource` can detach mid-recording, stall, emit zero frames, or exhaust the format chain — every row of §10.3 gets an automated test **with no scope attached** |
| Manual | Real scope on all three platforms, via a direct port *and* through the hub |

Detach-mid-recording is simultaneously the most important behaviour in the
app and the most tedious to verify by hand. Behind a fake source it is a CI
test.

Hardware verification stays manual. There is no honest way to test real USB
bandwidth negotiation without real USB.

## 12. Packaging

All three artifacts install without admin:

| Platform | Artifact | Notes |
|---|---|---|
| Windows | Portable folder / ZIP via `windeployqt` | Unzip and run. Optional per-user installer to `%LOCALAPPDATA%` |
| macOS | `.app` via `macdeployqt`, drag to `~/Applications` | **Requires Developer ID signing + notarization** or Gatekeeper blocks it outright. Also requires `NSCameraUsageDescription`, without which macOS kills the process on first camera access |
| Linux | AppImage via `linuxdeploy` | `chmod +x` and run |

**Code signing is the real gate, not privilege.** Unsigned Windows binaries
trigger SmartScreen until reputation accrues; unsigned macOS apps are blocked
entirely. Budget for certificates, and start the Apple Developer account
early — it is the top item in §6 for a reason.

## 13. Output files

- Snapshots: JPEG, full sensor resolution
- Video: H.264 in MP4, no audio track
- Destination: a plain user-visible folder, timestamped filenames, with a
  numeric suffix on same-second collisions so a double-tap cannot overwrite
- No database, no index, no metadata sidecar

## 14. Open questions

1. **Audio on recordings.** Assumed absent. If technicians want spoken notes,
   this changes the recording pipeline.
2. **High-zoom filtering.** Bilinear by default. Nearest-neighbour shows
   actual sensor pixels and is arguably more honest for evidence work.
3. **Crash/power-loss resilience for video.** See §10.1.

Resolved since revision 1: the commercial Qt licence question. With no
Android target there is no static linking, so LGPL is satisfied by shared
libraries on all three platforms.

## 15. Implementation

Plan: `docs/superpowers/plans/2026-09-30-microscope-desktop-app.md`
— 15 tasks, TDD throughout.

## 16. Revision history

**Revision 1 (2026-09-30).** Targets were Windows, macOS, Linux, and
**Android**; iOS was excluded for lack of external-USB-camera support.
Android required `libuvc`/`libusb` frame acquisition behind a JNI bridge plus
a `MediaCodec` encoder, estimated at ~60% of effort and nearly all technical
risk, and the plan opened with a mandatory 3-day de-risking spike. The
`ICaptureSource` seam was justified as a platform abstraction, and C++17 and
QML were both *forced* by Android.

**Revision 2 (2026-09-30).** Android dropped by decision. The spike, the
Android backend, the second encoder, and the phase-2 plan were all removed.
C++ and QML became preferences with stated reasons rather than constraints
(§5.1, §5.2); the framework choice was re-examined against a web stack now
that the four-platform argument no longer applies (§5.3); `ICaptureSource`
was re-justified on testability alone (§7.3); and the project's risk profile
shifted from one schedule-invalidating unknown to three manageable
operational items (§6).

**Revision 3 (2026-10-02).** View controls added after field use. Three
changes, all additive:

1. **Snapshot on Space.** The on-screen button was the only way to capture,
   which does not survive one-handed operation. Space is gated on the same
   condition as the button, and does not auto-repeat (a held key would write
   one JPEG per repeat). Incidentally this makes a USB footswitch or
   presenter remote work with no further code, since those enumerate as HID
   keyboards.
2. **On-screen zoom buttons** (§9.1). Zoom itself already existed via wheel
   and pinch; what was missing was a control reachable without a mouse or a
   second finger.
3. **Mirror and rotation** (§9.2). The first transform this project applies
   to saved files. It required amending §9's blanket "view-only" rule into a
   distinction with a reason: zoom crops and so cannot be baked in;
   orientation permutes pixels losslessly and so can.

Why the scope's own side button is NOT used: the microscope on the
development machine (`VID_05E3&PID_F12A`) presents a single UVC function and
no HID interface, so its button cannot be seen through Qt Multimedia at all.
Reaching it would need platform-specific code per OS — Windows KS events,
Linux `uvcvideo`'s input device (which needs a udev rule, i.e. root, against
§4's zero-admin requirement), and nothing public on macOS. A keyboard
shortcut plus an off-the-shelf HID footswitch covers the same need on all
three platforms with no platform code.
