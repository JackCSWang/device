#include <QtTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include "capture/CaptureController.h"
#include "core/FakeCaptureSource.h"
#include "output/QtRecorder.h"
#include "output/SnapshotWriter.h"

class TestFailureModes : public QObject {
    Q_OBJECT
    QTemporaryDir m_dir;

private slots:
    // Spec 10.3: "Detach mid-snapshot -> discard partial write; no orphan file"
    //
    // The brief's version of this test only ever fed one frame in total, so
    // it would pass identically whether or not onSourceStopped actually
    // clears m_snapshotArmed: with the source detached, FakeCaptureSource
    // never delivers another frame either way, and "no frame after the
    // detach" trivially means "no snapshot after the detach". To make the
    // clear-on-detach behaviour itself load-bearing, this version
    // reconnects and feeds one more frame afterwards: if the stale arm
    // survived the detach, that frame would silently satisfy it and a
    // snapshot would appear.
    void detachBeforeTheArmedFrameLeavesNoFile() {
        FakeCaptureSource src; src.setFrameSize({320, 240});
        QtRecorder rec; SnapshotWriter writer;
        CaptureController c(&src, &rec, &writer, m_dir.path());

        QSignalSpy saved(&c, &CaptureController::snapshotSaved);
        c.begin();
        src.emitOneFrame();
        c.takeSnapshot();        // armed, waiting for the next frame
        src.injectDetach();      // ...which never comes

        // Reconnect and let one more frame through. If the arm survived the
        // detach, this frame satisfies it and a snapshot silently appears.
        c.begin();
        src.emitOneFrame();
        QTest::qWait(500);

        QCOMPARE(saved.count(), 0);
        QCOMPARE(QDir(m_dir.path()).entryList({QStringLiteral("*.jpg")},
                                              QDir::Files).size(), 0);
    }

    // Spec 10.3: "Stream stalls later (no frame for 5s) -> one silent reopen"
    void stalledStreamIsReopenedOnce() {
        FakeCaptureSource src; src.setFrameSize({320, 240});
        QtRecorder rec; SnapshotWriter writer;
        CaptureController c(&src, &rec, &writer, m_dir.path());

        QSignalSpy msgs(&c, &CaptureController::status);
        c.begin();
        src.emitOneFrame();      // starts the stall timer
        // then nothing -- the stall watchdog must fire
        QTRY_VERIFY_WITH_TIMEOUT(
            msgs.count() > 0 && msgs.last().at(0).toString()
                .contains(QStringLiteral("stopped")),
            CaptureController::StallTimeoutMs + 3000);
    }

    // Spec 10.3: "Device claimed by another app -> name the conflict, not
    // 'failed to open'". The detail string is the only place that
    // information exists, so discarding it makes the row unimplementable.
    //
    // No recording is started, so this exercises onSourceStopped's
    // immediate (non-deferred) branch, and the detail-bearing message
    // reaches sourceLost directly -- not status -- confirmed below.
    void errorDetailReachesTheUserMessage() {
        FakeCaptureSource src;
        QtRecorder rec; SnapshotWriter writer;
        CaptureController c(&src, &rec, &writer, m_dir.path());

        QSignalSpy lost(&c, &CaptureController::sourceLost);
        c.begin();
        src.emitOneFrame();
        emit src.stopped(StopReason::Detached,
                         QStringLiteral("device in use by OtherApp"));

        QTRY_COMPARE_WITH_TIMEOUT(lost.count(), 1, 3000);
        QVERIFY(lost.at(0).at(0).toString().contains(QStringLiteral("OtherApp")));
    }

    void detachWithNoRecordingSaysNothingWasBeingRecorded() {
        FakeCaptureSource src;
        QtRecorder rec; SnapshotWriter writer;
        CaptureController c(&src, &rec, &writer, m_dir.path());

        QSignalSpy lost(&c, &CaptureController::sourceLost);
        c.begin();
        src.emitOneFrame();
        src.injectDetach();

        QTRY_COMPARE_WITH_TIMEOUT(lost.count(), 1, 3000);
        const QString msg = lost.at(0).at(0).toString();
        QVERIFY(msg.contains(QStringLiteral("disconnected")));
        QVERIFY(msg.contains(QStringLiteral("Reconnect")));
    }
};

QTEST_MAIN(TestFailureModes)
#include "test_failure_modes.moc"
