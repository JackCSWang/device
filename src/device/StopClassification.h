#pragma once
#include "core/ICaptureSource.h"

// QCamera::Error has only NoError and CameraError, and errorOccurred never
// fires with NoError -- so the error *code* cannot tell a detach apart from
// a busy device, a backend failure, or isochronous bandwidth starvation on a
// shared hub. Whether the device is still enumerated at the moment of the
// error is the only signal that separates them, and that distinction decides
// the wording the technician sees: "disconnected" sends them to check a
// cable that is fine.
//
// Both decisions here lived inside code reachable only with real hardware in
// a specific state -- QtCaptureSource's error lambda, and
// CaptureController's `#ifdef Q_OS_LINUX` block. docs/manual-test-matrix.md
// scenario 5 calls the first "the highest-priority untested item in the
// whole project", and the second cannot be executed on this project's only
// available toolchain at all. They are pure, so they belong where they can
// be unit-tested with no hardware and on any platform; the live test in
// test_qt_capture_source.cpp remains the integration check that
// QtCaptureSource actually asks the right questions.
class StopClassification {
public:
    static StopReason forCameraError(bool deviceStillEnumerated) {
        return deviceStillEnumerated ? StopReason::Error : StopReason::Detached;
    }

    // Spec 10.3's Linux `EACCES` row: is this stop diagnosable as a
    // device-node permission problem, so the app can name the literal
    // `usermod -aG video` fix instead of a generic "could not open"?
    //
    // Only for Error. A Detached device's node is gone, and a
    // not-readable answer for a path that no longer exists would turn every
    // ordinary unplug into a bogus permissions lecture -- which is the
    // mirror image of Important 1, where an unplug was misreported as a
    // privacy denial.
    //
    // Takes the two filesystem facts rather than a path, so the decision is
    // pure: the caller stats the node.
    static bool isDeviceNodePermissionProblem(StopReason reason, bool nodeExists,
                                              bool nodeReadable) {
        return reason == StopReason::Error && nodeExists && !nodeReadable;
    }
};
