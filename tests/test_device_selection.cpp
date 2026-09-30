#include <QtTest>
#include "device/DeviceSelection.h"

// Critical 1. Spec 8.5 step 1 requires "auto-open if exactly one scope,
// otherwise prompt"; the code bound devices.first() and there was no
// prompt, picker, setting or override anywhere. On the spec's own field
// hardware -- a technician with a laptop -- first() is the integrated
// webcam, and the execution ledger records exactly that happening: Task
// 10's hardware tests bound to "Integrated Camera", not the microscope.
//
// The descriptions used here are the real ones from the development
// machine (see docs/manual-test-matrix.md's hardware notes): the scope
// enumerates as "HD camera". That is why no description heuristic is
// acceptable -- every keyword rule anyone would reach for ("scope",
// "micro", "usb") picks the laptop's webcam over the actual microscope.
class TestDeviceSelection : public QObject {
    Q_OBJECT

    static ScopeDevice d(const QString& id, const QString& description) {
        return ScopeDevice{id, description, QCameraDevice()};
    }

private slots:
    void nothingEnumeratedIsNoDevice() {
        const auto choice = DeviceSelection::choose({}, QString());
        QCOMPARE(choice.kind, DeviceChoice::Kind::NoDevice);
    }

    // The single-scope case the spec asks to auto-open.
    void exactlyOneDeviceIsOpenedWithoutAsking() {
        const auto choice = DeviceSelection::choose(
            {d(QStringLiteral("scope"), QStringLiteral("HD camera"))}, QString());
        QCOMPARE(choice.kind, DeviceChoice::Kind::Open);
        QCOMPARE(choice.deviceId, QStringLiteral("scope"));
    }

    // The defect itself: two cameras, nothing remembered. Opening either is
    // a guess, and the guess the old code made was always the wrong one on
    // a laptop, because the built-in camera enumerates first.
    void twoDevicesWithNoMemoryPromptsRatherThanGuessing() {
        const auto choice = DeviceSelection::choose(
            {d(QStringLiteral("webcam"), QStringLiteral("Integrated Camera")),
             d(QStringLiteral("scope"), QStringLiteral("HD camera"))},
            QString());
        QCOMPARE(choice.kind, DeviceChoice::Kind::Prompt);
        QVERIFY(choice.deviceId.isEmpty());
    }

    // Having chosen once, the technician is not asked again.
    void aRememberedDeviceThatIsPresentWinsOverTheFirstInTheList() {
        const auto choice = DeviceSelection::choose(
            {d(QStringLiteral("webcam"), QStringLiteral("Integrated Camera")),
             d(QStringLiteral("scope"), QStringLiteral("HD camera"))},
            QStringLiteral("scope"));
        QCOMPARE(choice.kind, DeviceChoice::Kind::Open);
        QCOMPARE(choice.deviceId, QStringLiteral("scope"));
    }

    // A remembered scope that is not plugged in must not suppress the
    // prompt, or the app would sit idle with two usable cameras present.
    void aRememberedDeviceThatIsAbsentFallsBackToPrompting() {
        const auto choice = DeviceSelection::choose(
            {d(QStringLiteral("webcam"), QStringLiteral("Integrated Camera")),
             d(QStringLiteral("other"), QStringLiteral("Capture Card"))},
            QStringLiteral("scope"));
        QCOMPARE(choice.kind, DeviceChoice::Kind::Prompt);
    }

    // ... but with only one present, that one is still opened: the spec's
    // rule is about ambiguity, and there is none here.
    void aRememberedDeviceThatIsAbsentStillAutoOpensASingleDevice() {
        const auto choice = DeviceSelection::choose(
            {d(QStringLiteral("webcam"), QStringLiteral("Integrated Camera"))},
            QStringLiteral("scope"));
        QCOMPARE(choice.kind, DeviceChoice::Kind::Open);
        QCOMPARE(choice.deviceId, QStringLiteral("webcam"));
    }

    // The remembered id is matched by id, never by description: two
    // identical scope models on one machine share a description and must
    // not be confused for one another.
    void identicalDescriptionsAreDistinguishedById() {
        const auto choice = DeviceSelection::choose(
            {d(QStringLiteral("scope-a"), QStringLiteral("HD camera")),
             d(QStringLiteral("scope-b"), QStringLiteral("HD camera"))},
            QStringLiteral("scope-b"));
        QCOMPARE(choice.kind, DeviceChoice::Kind::Open);
        QCOMPARE(choice.deviceId, QStringLiteral("scope-b"));
    }
};

QTEST_MAIN(TestDeviceSelection)
#include "test_device_selection.moc"
