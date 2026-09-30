#include <QtTest>
#include "device/CameraAccessPolicy.h"

// Task-15 review, Important 3: this decision used to live inline in
// AppContext (wiring-only by this project's own rule) with no coverage of
// its own -- which is how Important 1's stale-cameraAccessDenied bug got
// in: the "reset when a device reappears" half of the decision was simply
// never written, and nothing here would have caught it. These tests pin
// the decision itself, independent of AppContext's use of it.
class TestCameraAccessPolicy : public QObject {
    Q_OBJECT

private slots:
    // No camera was ever seen, and none is present now: an absent scope,
    // not a permissions problem.
    void noDeviceEverSeenIsNotAccessDenied() {
        QCOMPARE(CameraAccessPolicy::classify(false, 0), DeviceAbsenceReason::NoScopeEverSeen);
    }

    // A camera was seen at startup but the list is empty now: the
    // signature this whole feature exists to detect.
    void deviceSeenAtStartupThenGoneIsAccessDenied() {
        QCOMPARE(CameraAccessPolicy::classify(true, 0),
                 DeviceAbsenceReason::LikelyAccessDenied);
    }

    // A non-empty current list is never classified as an absence, whether
    // or not anything was seen at startup -- this is what makes the
    // "reset on reappearance" call in AppContext correct rather than an
    // arbitrary policy choice duplicated on the caller's side.
    void nonEmptyListIsNeverAnAbsence() {
        QCOMPARE(CameraAccessPolicy::classify(true, 1), DeviceAbsenceReason::NoScopeEverSeen);
        QCOMPARE(CameraAccessPolicy::classify(false, 1), DeviceAbsenceReason::NoScopeEverSeen);
    }
};

QTEST_MAIN(TestCameraAccessPolicy)
#include "test_camera_access_policy.moc"
