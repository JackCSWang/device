#pragma once

// Spec 10.3's camera-privacy rows need two decisions, both pure, both here.
//
// This used to be inline in AppContext -- wiring-only by this project's own
// rule -- with no test of its own. That is how it grew a stale-state bug
// (task-15-report.md, Important 1 & 3): the "reset when a device reappears"
// half of the decision was simply never written, and nothing caught it
// because nothing exercised the decision in isolation. Extracted here so it
// can be.
enum class DeviceAbsenceReason { NoScopeEverSeen, LikelyAccessDenied };

class CameraAccessPolicy {
public:
    // Decision 1 -- the list came up empty. Is that "no scope was ever
    // here" or "one was here and is now being withheld by a privacy gate"?
    //
    // sawDeviceAtStartup: whether the OS enumerated at least one camera the
    //   moment this app started, before anything here could have caused a
    //   denial.
    // currentDeviceCount: how many the registry reports right now.
    // everOpenedADevice: whether *this app* has ever successfully opened a
    //   capture device since startup.
    //
    // That third input is what stops the normal case being misreported. A
    // technician plugs the scope in before launching the app, so
    // sawDeviceAtStartup is true essentially always; on a plain unplug the
    // two-input version therefore returned LikelyAccessDenied and offered
    // "Open camera privacy settings" for a cable lying on the bench. If a
    // pipeline ever ran, the app demonstrably had access, so an empty list
    // now is a detach and nothing else.
    //
    // Meaningful to call at any device count -- a non-zero count is never
    // classified as an absence -- but callers typically only need this when
    // currentDeviceCount is 0, since a non-empty list already answers its
    // own question without asking.
    static DeviceAbsenceReason classify(bool sawDeviceAtStartup, int currentDeviceCount,
                                        bool everOpenedADevice);

    // Decision 2 -- activation failed while the device is *still*
    // enumerated. Decision 1 cannot see this case at all: it only ever
    // triggers on an empty list, and on Windows a privacy-blocked camera
    // still enumerates perfectly well and fails later, at activation. That
    // is why spec 10.3's `ms-settings:privacy-webcam` row had, in practice,
    // no way to fire. macOS AVFoundation behaves the same way for a
    // never-granted first launch (see docs/manual-test-matrix.md scenario
    // 11's platform note).
    //
    // This is deliberately a *candidate* denial, not a diagnosis: a busy
    // device and a bandwidth failure reach the same state, and QCamera::
    // Error carries no code that separates them. The caller's contract is
    // therefore to offer the privacy deep link *alongside* the driver's own
    // reported cause, never instead of it.
    //
    // deliveredAFrameThisPipeline is what keeps it honest: a pipeline that
    // was streaming and then broke had access by demonstration, so its
    // failure is never a privacy block.
    static bool activationFailureMayBeAccessDenied(bool deviceStillEnumerated,
                                                   bool deliveredAFrameThisPipeline);
};
