#include <QtTest>
#include "device/CameraAccessPolicy.h"
#include "device/StopClassification.h"

// Task-15 review, Important 3: this decision used to live inline in
// AppContext (wiring-only by this project's own rule) with no coverage of
// its own -- which is how Important 1's stale-cameraAccessDenied bug got
// in: the "reset when a device reappears" half of the decision was simply
// never written, and nothing here would have caught it. These tests pin
// the decision itself, independent of any caller's use of it.
class TestCameraAccessPolicy : public QObject {
    Q_OBJECT

private slots:
    // No camera was ever seen, and none is present now: an absent scope,
    // not a permissions problem.
    void noDeviceEverSeenIsNotAccessDenied() {
        QCOMPARE(CameraAccessPolicy::classify(false, 0, false),
                 DeviceAbsenceReason::NoScopeEverSeen);
    }

    // A camera was seen at startup, this app never opened one, and the list
    // is empty now: the signature this whole feature exists to detect.
    void deviceSeenAtStartupThenGoneWithoutEverOpeningIsAccessDenied() {
        QCOMPARE(CameraAccessPolicy::classify(true, 0, false),
                 DeviceAbsenceReason::LikelyAccessDenied);
    }

    // A non-empty current list is never classified as an absence, whether
    // or not anything was seen at startup -- this is what makes the
    // "reset on reappearance" call in CaptureSession correct rather than an
    // arbitrary policy choice duplicated on the caller's side.
    void nonEmptyListIsNeverAnAbsence() {
        QCOMPARE(CameraAccessPolicy::classify(true, 1, false),
                 DeviceAbsenceReason::NoScopeEverSeen);
        QCOMPARE(CameraAccessPolicy::classify(false, 1, false),
                 DeviceAbsenceReason::NoScopeEverSeen);
        QCOMPARE(CameraAccessPolicy::classify(true, 1, true),
                 DeviceAbsenceReason::NoScopeEverSeen);
    }

    // Final review, Important 1 -- the two rows the third input exists for.
    //
    // The normal field case: the technician plugs the scope in *before*
    // launching, so sawDeviceAtStartup is true essentially always. Unplug
    // the cable and the two-input version returned LikelyAccessDenied, so
    // the banner offered "Open camera privacy settings" for a cable lying
    // on the bench. A pipeline that ever ran proves this app had access.
    void anUnplugAfterAWorkingPipelineIsADetachNotADenial() {
        QCOMPARE(CameraAccessPolicy::classify(/*sawDeviceAtStartup=*/true,
                                              /*currentDeviceCount=*/0,
                                              /*everOpenedADevice=*/true),
                 DeviceAbsenceReason::NoScopeEverSeen);
    }

    // And the case that makes the input load-bearing rather than merely
    // suppressive: having *never* opened a device keeps the denial
    // detection working, so the fix does not silently delete spec 10.3's
    // macOS/Windows privacy rows.
    void neverHavingOpenedADeviceStillDetectsADenial() {
        QCOMPARE(CameraAccessPolicy::classify(true, 0, false),
                 DeviceAbsenceReason::LikelyAccessDenied);
        QCOMPARE(CameraAccessPolicy::classify(false, 0, true),
                 DeviceAbsenceReason::NoScopeEverSeen);
    }

    // Final review, Important 3: the Windows case. A privacy-blocked camera
    // still enumerates there and fails at activation, so classify() -- which
    // only ever fires on an empty list -- can never see it, and the
    // `ms-settings:privacy-webcam` row had no way to trigger. A failed
    // activation with the device still enumerated is a *candidate* denial.
    void aFailedActivationWithTheDeviceStillEnumeratedIsACandidateDenial() {
        QVERIFY(CameraAccessPolicy::activationFailureMayBeAccessDenied(
            /*deviceStillEnumerated=*/true, /*deliveredAFrameThisPipeline=*/false));
    }

    // A device that vanished is a detach, not a denial -- the deep link
    // would send the technician to the wrong place.
    void aFailedActivationOnAVanishedDeviceIsNotADenial() {
        QVERIFY(!CameraAccessPolicy::activationFailureMayBeAccessDenied(false, false));
    }

    // A pipeline that was streaming and then broke had access by
    // demonstration, so its failure is never a privacy block. Without this
    // input, every mid-session driver error would offer the privacy link.
    void aPipelineThatWasStreamingIsNeverADenial() {
        QVERIFY(!CameraAccessPolicy::activationFailureMayBeAccessDenied(true, true));
        QVERIFY(!CameraAccessPolicy::activationFailureMayBeAccessDenied(false, true));
    }

    // Must-fix minor 4: the Detached-vs-Error mapping, extracted out of
    // QtCaptureSource's error lambda so it is testable with no hardware.
    // "Device still enumerated at error time => Error, else Detached" is
    // the whole rule, and it decides whether the technician is sent to
    // check a cable that is fine (docs/manual-test-matrix.md scenario 5).
    void aStillEnumeratedDeviceErrorIsNotADetach() {
        QCOMPARE(StopClassification::forCameraError(true), StopReason::Error);
        QCOMPARE(StopClassification::forCameraError(false), StopReason::Detached);
    }
};

QTEST_MAIN(TestCameraAccessPolicy)
#include "test_camera_access_policy.moc"
