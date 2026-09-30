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

These ten scenarios either don't fit a single Pass/Fail cell in Part A, or
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

### 4. Cable reseat during the deferred-teardown window

**Setup:** trigger scenario 1 (detach mid-recording), then — while the app
is still in the few-second window where the encoder is finalizing and
teardown is deliberately deferred — reseat (replug) the cable.

**Expected:** the app recovers cleanly: no dead/unresponsive UI, and once
teardown completes the app is ready to start a new session against the
reconnected (or a newly enumerated) device.

**Why this is the priority verification item:** a fix for a bug in exactly
this window (reseating during deferred teardown previously left the UI dead
until restart) has **no automated test** — the code has no seam to inject a
fake device registry mid-teardown. This manual run is the *only*
verification that fix will ever receive. Do not skip it, and do not assume
it still works just because it worked once.

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

**Why this is the highest-priority *untested* item in the whole project:**
the classification logic that tells a stop-because-detached apart from a
stop-because-busy/backend-error is correct only by inspection
(`onSourceStopped`'s `StopReason` handling in `CaptureController.cpp`). No
live test exercises the busy/error path — `FakeCaptureSource` can simulate
a detach but not a real driver-level "device busy" error. This scenario is
the only way that logic gets exercised against reality before release.

**Explicit fail condition — total silence:** `onSourceStopped` now has a
branch for `StopReason::Error` while nothing is recording (Task 15), so a
banner reading "The scope stopped. Nothing was being recorded. Reconnect
the scope to continue." — plus, whenever the driver supplies one, a
trailing "Reported cause: …" naming the conflict — is the expected
behaviour again. **Total silence here is a regression** and must be marked
**Fail** on sight; it would mean the Task 15 fix itself broke. (Silence was
the true pre-Task-15 behaviour, which is why this note originally existed —
see git history for the prior wording.)

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
- **The Linux permission check only ever looks at `/dev/video0`.**
  `CaptureController::begin()`'s `Q_OS_LINUX` branch hardcodes that one
  device node (per the Task 13 brief). If the scope enumerates at
  `/dev/video1` or higher — plausible on a machine with a built-in webcam
  or a second capture device present — an actual permission problem on the
  real device node will **not** be detected, and the technician gets the
  generic "could not open the scope" message instead of the actionable
  `usermod -aG video` hint. Note which device node the scope actually
  enumerated as (`v4l2-ctl --list-devices` or equivalent) on every Linux
  matrix run, and treat a missed permission hint on a non-`/dev/video0`
  device as an expected gap, not a surprise failure.
