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
// This one-line mapping lived inside QtCaptureSource's error lambda, which
// needs real hardware in a specific state to reach. docs/
// manual-test-matrix.md scenario 5 calls it "the highest-priority untested
// item in the whole project" for exactly that reason. It is pure, so it
// belongs where it can be unit-tested with no hardware at all; the live test
// in test_qt_capture_source.cpp remains the integration check that
// QtCaptureSource actually asks the right question.
class StopClassification {
public:
    static StopReason forCameraError(bool deviceStillEnumerated) {
        return deviceStillEnumerated ? StopReason::Error : StopReason::Detached;
    }
};
