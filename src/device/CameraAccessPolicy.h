#pragma once

// Spec 10.3's camera-privacy row needs one decision: when the device list
// comes up empty, is that "no scope was ever attached" or "one was seen at
// startup and this app is now being denied access to it" (macOS Settings >
// Privacy > Camera, Windows camera privacy)? Only the second case leaves a
// trace -- a device existed once, at startup, and does not now.
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
    // sawDeviceAtStartup: whether the OS enumerated at least one camera the
    // moment this app started, before anything here could have caused a
    // denial. currentDeviceCount: how many the registry reports right now.
    // Meaningful to call at any device count -- a non-zero count is never
    // classified as an absence -- but callers typically only need this when
    // currentDeviceCount is 0, since a non-empty list already answers its
    // own question without asking.
    static DeviceAbsenceReason classify(bool sawDeviceAtStartup, int currentDeviceCount);
};
