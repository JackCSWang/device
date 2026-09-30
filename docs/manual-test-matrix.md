# Manual test matrix

This is the release checklist for things a real USB scope, a real display
compositor, and real touch/pointer hardware are needed to verify. Everything
that *can* be exercised by `FakeCaptureSource` or a headless QML test has an
automated test instead (`ctest --test-dir build`) and is **not** repeated
here.

Run the full matrix, on real hardware, on **every** platform before every
release — including Windows, where most of this project's automated
verification happened. An automated pass on one platform says nothing about
the other two; §10.3/§11 of the design spec is explicit that hardware
verification stays manual because there is no honest way to fake USB
isochronous bandwidth negotiation.

## Part A — release-gate matrix

One row per platform. Fill in Pass/Fail plus a one-line note (build number,
scope model, hub used) for every release candidate. All eleven columns must
pass before a release ships.

| Platform | 1. Installs without admin | 2. Scope enumerates | 3. Live view renders | 4. Pinch/wheel zoom, focus point stays put | 5. Pan clamps at edges; no pan at fit | 6. Snapshot is full-sensor at 8x | 7. Recording plays at correct speed | 8. Unplug mid-recording: file plays, banner names it | 9. Direct USB port | 10. Through the 6-in-1 hub | 11. Output folder opens |
|---|---|---|---|---|---|---|---|---|---|---|---|
| Windows | | | | | | | | | | | |
| macOS | | | | | | | | | | | |
| Linux | | | | | | | | | | | |

Columns 9 and 10 cannot be automated under any circumstances: real
isochronous bandwidth negotiation requires a real USB controller arbitrating
real devices on a real hub. There is no simulation of this that would be
honest.

Column 6 is the spec's central promise (design §9): zoom is view-only, so
whatever is on screen at 8x, the saved JPEG must still be the complete
sensor frame, not a crop of the zoomed viewport.

Column 8 is covered in more depth as scenario 1 in Part B below — it is the
single most important row in this entire document.

## Part B — extended scenarios

These fifteen scenarios either don't fit a single Pass/Fail cell in Part A, or
were flagged during Task 13 as **verified only by code inspection**, with no
automated test and no live exercise yet. Each needs its own row in the
release notes: platform, scope model, hub (if applicable), Pass/Fail, and
free-text notes. Treat any Fail here as release-blocking regardless of what
Part A shows — several of these guard code paths that have never been
exercised outside of review.

### 1. Detach mid-recording — highest priority row in this document

**Setup:** start recording, let it run a few seconds so there's real content,
then physically unplug the USB cable.

**Expected:** the recorder finalizes (writes the `moov` atom), and the
status banner reports the recording was saved **and names the exact
filename**. The saved file must actually play back in a normal video player
— open it and check, don't just trust the banner. "Device disconnected"
without a filename is a fail even if a file happens to exist on disk,
because the point of the message is to tell the technician where the
evidence is (design §10.1).

**Why it matters:** this is the defining field failure per the spec — a
technician leans over the sample and the cable pops out mid-take. A
technician who is told nothing was lost, and is wrong, discovers that in
the field with no way to redo the shot.

### 2. Detach in the gap between pressing Record and the first frame

**Setup:** unplug the cable in the narrow window after pressing Record but
before the first frame has arrived (the recorder has been armed but nothing
has been written yet). This is timing-sensitive — you may need a few
attempts, or use a scope/hub combination that opens slowly.

**Expected:** the in-progress file is deleted (no zero-byte or corrupt
orphan left in the output folder), and the banner says the recording
**could not be saved** — it must not claim a save happened. This is the
mirror case of scenario 1: same interruption, but no content ever existed,
so the correct message is failure, not a filename.

**Fail condition:** any banner wording that could read as "your recording is
saved" when no playable file exists.

### 3. Mid-stream stall while recording

**Setup:** start recording, then interrupt the video stream without a clean
USB detach — e.g. put the scope's process to sleep via a USB hub that
power-cycles the port, or another means of freezing frame delivery without
pulling the cable. The controller's stall watchdog (`StallTimeoutMs`, 5 s
with no frame) should fire.

**Expected:** the banner reports that the recording was **ended and saved**,
and explicitly states that recording will **not** resume automatically —
the technician must press Record again once the stream is healthy. Compare
against the exact wording in `CaptureController.cpp`'s stall branch of the
recorder's `finished`/`failed` handlers.

**Two deliberate deviations from spec §10.3's wording** ("one silent reopen
attempt, then surface"), both pre-existing and both now recorded rather
than left to be rediscovered:

1. The reopen is **announced** ("The video stream stopped. Reconnecting."),
   not silent. Spec §10 also says every failure must state what happened,
   and a live view that freezes for five seconds and then resumes with no
   explanation is exactly what generates a support call.
2. It is **unbounded** across repeated stalls, not one attempt. A stall is
   recoverable and a scope that stalls twice is not thereby broken.

Both halves of the row are now automated —
`test_failure_modes::aStalledStreamIsReopenedAndKeepsBeingWatched` (the
reopen actually delivers a frame again, and the watchdog re-arms) and
`::aReopenThatDeliversNothingSurfacesViaTheFirstFrameWatchdog` (the "then
surface" half). The previous test asserted only that some message contained
"stopped", and stayed green with the entire reopen deleted.

### 4. Cable reseat during the deferred-teardown window

**Setup:** trigger scenario 1 (detach mid-recording), then — while the app
is still in the few-second window where the encoder is finalizing and
teardown is deliberately deferred — reseat (replug) the cable.

**Expected:** the app recovers cleanly: no dead/unresponsive UI, and once
teardown completes the app is ready to start a new session against the
reconnected (or a newly enumerated) device.

**Why this used to be the priority verification item, and what changed:** a
fix for a bug in exactly this window (reseating during deferred teardown
previously left the UI dead until restart) had **no automated test** —
AppContext had no seam to inject a fake device registry mid-teardown. The
final fix wave extracted that lifecycle into `CaptureSession` behind an
`IDeviceRegistry` seam, and this case is now covered by
`test_capture_session::aReseatInsideTheDeferredTeardownWindowRecovers`,
which asserts that the attach edge is *swallowed* (proving the one-shot
re-poll is the only thing that can recover it) and then that recovery
happens. Run this row anyway: the automated version fakes the registry, so
only a real reseat exercises the OS's own `videoInputsChanged` timing.

### 5. Device-busy or backend error with the scope still plugged in

**Setup:** provoke a backend/open failure while the physical cable stays
connected — e.g. open the scope in another application (a webcam app,
OBS, a browser tab) first, then try to start the live view in Microscope so
the device is reported busy; or trigger any other backend error that isn't
a physical detach.

**Expected:** the message must say the scope **stopped** (or an equivalent
"something went wrong with the running device" framing) — it must **not**
say the scope was **disconnected**, since it is still physically attached
and a "disconnected" message sends the technician checking a cable that is
fine.

**Why this matters and what is now automated:** the classification that
tells a stop-because-detached apart from a stop-because-busy is now a pure
helper, `StopClassification::forCameraError()`, with unit tests that need no
hardware (`test_camera_access_policy::aStillEnumeratedDeviceErrorIsNotADetach`),
and the wording downstream of it is covered by
`test_failure_modes::errorDuringRecordingNamesTheCauseAndEndsTheTake`.

A **live** test also exists —
`test_qt_capture_source::aBusyDeviceIsAnErrorAndIsNotCalledDisconnected`,
which holds the device open on one handle and opens it from a second — but
it **SKIPs on the Windows development machine**: Qt 6.8.3's `ffmpeg`
multimedia backend there permits two concurrent readers of one camera, so a
"device busy" condition cannot be provoked at all. Record on every platform
run whether that test skips or actually asserts; where it skips, this manual
row is still the only verification.

**Explicit fail condition — total silence:** `onSourceStopped` has a branch
for `StopReason::Error` while nothing is recording (Task 15), so a banner
reading "The scope stopped. Nothing was being recorded. Reconnect the scope
to continue." — plus, whenever the driver supplies one, a trailing "Reported
cause: …" naming the conflict — is the expected behaviour. **Total silence
here is a regression** and must be marked **Fail** on sight.
### 6. Resize the window while panned at high zoom

**Setup:** zoom in (well above 1x, ideally near 8x) and pan away from
center so the viewport shows an off-center region of the frame. Then resize
the application window (drag an edge/corner), including making it
significantly smaller and significantly larger, and try both directions
repeatedly.

**Expected:** the pan offset adjusts continuously with the resize — it must
not jump to a different part of the frame and must not silently reset/drop
back to center. If the new window size makes the current pan invalid (e.g.
the viewport would show outside the frame), it should re-clamp smoothly, not
snap.

**Why it needs a human:** Qt emits no warning or assertion for this defect
class — a pan value that's numerically valid but visually wrong (jumped or
reset) passes every type check. Only a person watching the frame during the
resize will notice.

### 7. Pinch-zoom on a touchscreen

**Setup:** requires touchscreen hardware (e.g. a Windows 2-in-1, per design
§3.1's remark that touch remains supported even though mobile OSes are not
targets). Pinch out to zoom in, then pinch back in to zoom back out.

**Expected:** zoom level tracks the pinch gesture smoothly and
proportionally throughout the gesture (no stepping or lag that makes it feel
disconnected from your fingers). Pinching back in must **restore the
previous zoom level along the same path** — it must not overshoot or "race"
to the 8x clamp and get stuck there. Also check that the zoom centers on
the pinch centroid, not a fixed point.

### 8. Snapshot while zoomed to 8x

**Setup:** zoom the live view in to 8x (or as close to it as the device
supports — see the hardware note below), pan to an off-center region, then
take a snapshot.

**Expected:** the saved JPEG is the **full sensor frame at full sensor
resolution** — not the zoomed/panned crop currently on screen. Open the
saved file and confirm it shows the entire field of view the scope
captures, not just what was visible in the 8x viewport.

**Why it matters:** this is the spec's central promise (design §9): "Zoom is
view-only. Every snapshot and every recording contains the full sensor
frame at full resolution, regardless of zoom or pan state." A regression
here silently breaks the one guarantee the whole zoom feature is built
around.

### 9. Through the shared USB hub as well as a direct port

**Setup:** run the *entire* matrix once with the scope on a direct USB
port, and again with it behind a shared/multiport hub (design §10.2 calls
out a 6-in-1 hub with a card reader specifically, as that is the known
problem configuration).

**Expected on the hub:** if bandwidth is insufficient, the app must not
just sit at a black screen with no explanation. The frame-arrival watchdog
should step down the format preference list and retry; if every format in
the list is exhausted, the app must report "no video received" and name
the formats the scope advertised, suggesting a direct port instead.

**Why it matters:** UVC's isochronous transfers reserve bandwidth up front.
A hub with several devices attached can fail this negotiation invisibly —
`open()` can succeed while no frame ever arrives. This is columns 9/10 in
Part A, expanded: it is the one failure mode with literally no honest way
to simulate it, so it must be run for real, on real hardware, on every
platform, every release.

### 10. The Record button after a failed take

**Setup:** cause a recording to fail by any means (detach mid-recording,
detach before first frame, a stall, a backend error — any scenario above
that ends a recording abnormally).

**Expected:** once the failure is reported, the Record button returns to
its idle "Record" label/state. It must not stay latched on "Stop
recording" — a latched button implies the app still thinks it's recording,
which would mislead the technician into believing evidence is still being
captured when it is not.

### 11. Camera access denied by OS privacy settings, then granted

**Setup:** with the scope attached, deny this app's camera permission in
the OS's privacy settings (or run before ever granting it), launch the app,
then grant permission and reattach/reopen without restarting the app.

**Expected:** the status banner names the permission problem (not "no scope
detected") and shows an "Open camera settings" button; after granting
access and reattaching, the button disappears and the live view starts
normally — it must not stay stuck visible over a working pipeline.

**Platform note — cheap to log, not worth chasing:** the denial detection
(`AppContext::m_sawDeviceAtStartup` / `CameraAccessPolicy`) relies on the
device list being non-empty at startup and empty once denied. This is a
heuristic: on macOS, AVFoundation's enumeration does not necessarily hide a
device behind the privacy gate the way this assumes, so a **never-granted**
first launch (permission denied before the app ever ran, rather than
revoked mid-session) may not trip `cameraAccessDenied` there. Record
whether it does on each macOS run; do not treat a miss here as a release
blocker on its own.


### 12. Two cameras present — the scope must not be guessed

**Setup:** a laptop with a built-in webcam, plus the USB scope. Delete the
remembered device first (`device/lastUsedId` under the app's `QSettings` —
on Windows, `HKCU\Software\WTC\Microscope`), then launch.

**Expected:** nothing opens. The banner says "Select a scope." and a picker
lists both cameras by their OS descriptions. Choosing the scope opens it,
and the choice survives a restart — relaunch with both cameras present and
the scope must open with no prompt. Unplug the scope, relaunch, and the
webcam (now the only input) must open on its own.

**Why it matters:** this was Critical 1. The old code bound
`devices.first()`, which on this hardware is "Integrated Camera" — the
execution ledger records Task 10's hardware tests binding to it rather than
the microscope. Everything else in the app is vacuous when the wrong device
is open: unplugging the scope mid-recording does nothing, because the scope
was never the source.

**Fail conditions:** any automatic choice between two cameras; a prompt that
reappears after a device has been chosen; the picker listing zero or one
entry when two cameras are attached; or the app opening a device whose
description it guessed from a keyword (the scope here enumerates as "HD
camera", so every plausible keyword rule picks the webcam).

### 13. A persistently busy or erroring device must not loop

**Setup:** hold the scope open in another application so it stays
enumerated but cannot be activated (see scenario 5 for how; if that backend
allows two readers, use any other means of making activation fail while the
device stays listed). Then launch Microscope, or press Retry, and **watch
the banner for at least 30 seconds**.

**Expected:** one open attempt, one message naming the driver's cause, and
then nothing. The banner must sit still. A Retry button is offered, and
pressing it makes exactly one further attempt.

**Fail condition — the banner alternating** between an error and "Connected
to …", or CPU sitting at a constant load with no video: that is Critical 3
back. It ran one full open → error → teardown → open cycle per event-loop
turn, forever. `test_capture_session::aPersistentlyErroringDeviceIsNotReopenedInALoop`
covers the logic with a fake registry; this row is the check against a real
driver, whose error timing is its own.

### 14. An unplug that produces no camera error

**Setup:** unplug the scope while the app is idle (not recording), on a
machine and driver combination where `QCamera::errorOccurred` does **not**
fire — some backends only report the device vanishing through
`QMediaDevices::videoInputsChanged`. Try it both with the scope as the only
camera and with a second camera present.

**Expected:** the banner updates to "No scope detected. Connect the
microscope by USB." (or opens the remaining camera, if one is present and
remembered). It must **not** offer "Open camera privacy settings".

**Why it matters:** this was Important 1. `cameraAccessDenied` was true
whenever any camera existed at launch, which is the normal case since the
scope is plugged in before the app starts — so an ordinary unplug offered a
privacy-settings button for a cable lying on the bench.
`CameraAccessPolicy::classify()` now also takes whether this app ever
successfully opened a device; a pipeline that ran proves access was
granted. Note on every run whether the detach arrived as a camera error, as
a device-list change, or both.

### 15. Windows camera-privacy denial — does it even detect?

**Setup:** on Windows, with the scope attached, deny camera access in
Settings → Privacy & security → Camera, then launch the app.

**Expected:** the banner names a permissions problem and an "Open camera
settings" button appears, deep-linking to `ms-settings:privacy-webcam`
(spec §10.3). After granting access and pressing Retry (or reattaching),
the button disappears and the live view starts.

**The specific doubt to record:** the original detection required the device
list to come back **empty** when denied. On Windows a privacy-blocked
camera generally still enumerates and fails at *activation*, so that
condition never held and the deep link had no way to appear. The fix adds a
second, independent trigger — `CameraAccessPolicy::activationFailureMayBeAccessDenied()`:
an activation failure with the device **still enumerated** and no frame
ever delivered is treated as a *candidate* denial, and the settings link is
offered **alongside** the driver's reported cause, never instead of it.

So record three things on every Windows run: (a) whether the device still
enumerates while denied, (b) whether the button appears, and (c) whether
the driver's cause is still shown next to it. A busy device reaches the same
state, which is why the wording must stay "you may not be allowed to use
the camera" alongside the real cause rather than asserting a denial. Do not
treat a *false positive* on a busy device as a release blocker; do treat a
missing button on a genuine denial as one.

## Known hardware facts

Record these against every matrix run, because they change what a pass
looks like on the hardware actually available:

- **The USB microscope on the Windows dev machine used for Task 13**
  (`VID_05E3&PID_F12A`, enumerating as "HD camera") advertises **only
  640x480 YUYV** at 5/15/30 fps — no MJPEG, no higher resolution. Every
  snapshot and recording from this device is therefore 0.3 MP, and the
  1x-8x zoom range is largely decorative on this hardware: at 8x the
  viewport shows roughly 80x60 real sensor pixels. Do not mistake soft/
  blocky imagery at high zoom on this device for a defect — that is
  expected given the sensor. A higher-resolution scope should be used at
  least once per release to confirm zoom is actually useful on real
  hardware, not just correct.
- **Both cameras tested (Task 13) report identity rotation and no
  mirroring**, so the full-sensor-frame guarantee (scenario 8 above) holds
  on this hardware by observation, not by any code path that corrects for
  rotation. A device that *did* report a rotation transform would silently
  violate the full-sensor-frame guarantee, because `QVideoFrame::toImage()`
  applies the frame's transform when converting to an image — so a rotated
  frame would be saved rotated/cropped relative to the sensor's raw output.
  This has never been tested, because no device that reports non-identity
  rotation was available. If one becomes available, scenario 8 should be
  re-run against it specifically and the result logged here regardless of
  outcome.
- **macOS and Linux have never been built or tested**, on any hardware, as
  of Task 13 — this development machine has no toolchain for either
  platform (see `packaging/macos/build-app.sh` and
  `packaging/linux/build-appimage.sh`, both written to spec and unverified).
  Every row in this document, Part A and Part B alike, must be run fresh
  the first time either platform is built, with no assumption carried over
  from the Windows results.
- **The Linux permission check now uses the device's real node.**
  `CaptureController::permissionHintForError()` asks the source for
  `deviceNode()` (on V4L2, `QCameraDevice::id()` *is* the `/dev/videoN`
  path) instead of hardcoding `/dev/video0`, and it runs on the
  `StopReason::Error` path rather than on `!start()`, which activation's
  asynchrony made unreachable. Still note which node the scope enumerated
  as (`v4l2-ctl --list-devices`) on every Linux run — this code has never
  been executed on Linux, because no Linux toolchain exists on the
  development machine. The decision itself is unit-tested on Windows
  (`StopClassification::isDeviceNodePermissionProblem`); only the
  `QFileInfo` stat around it is unverified.
