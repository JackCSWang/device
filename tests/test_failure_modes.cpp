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

    // Spec 10.3: "Stream stalls later (no frame for 5 s) -> One silent
    // reopen attempt, then surface".
    //
    // Important 6: the old version of this test asserted only that *some*
    // status message contained "stopped" -- so deleting both
    // m_source->start() and m_firstFrameTimer.start() from the stall
    // handler left it green, which makes it the sixth insensitive test
    // found in this project. It proved the stall watchdog fired and nothing
    // whatever about the reopen it exists to perform.
    //
    // This half asserts the reopen: after the stall, a frame must actually
    // be delivered again (proved by a snapshot succeeding, which requires
    // CaptureController to have seen a frame), and the stall watchdog must
    // re-arm so a *second* stall is caught too.
    //
    // Sensitivity: delete m_source->start() from the stall handler and this
    // fails -- FakeCaptureSource::emitOneFrame() is a no-op while stopped,
    // so no frame arrives, no snapshot is saved, and the second stall
    // message never comes. Verified by removing and restoring it.
    //
    // DEVIATION FROM THE SPEC'S WORDING, deliberate and pre-existing: the
    // reopen is *announced* ("The video stream stopped. Reconnecting."),
    // not silent, and it is unbounded across repeated stalls rather than a
    // single attempt. Announcing it is the right call under spec 10's own
    // rule that every failure states what happened -- a live view that
    // freezes for five seconds and then resumes with no explanation is the
    // kind of thing that generates a support call. Unbounded is right
    // because a stall is recoverable and a scope that stalls twice is not
    // thereby broken. Recorded here rather than silently differing.
    void aStalledStreamIsReopenedAndKeepsBeingWatched() {
        FakeCaptureSource src; src.setFrameSize({320, 240});
        QtRecorder rec; SnapshotWriter writer;
        CaptureController c(&src, &rec, &writer, m_dir.path());

        QSignalSpy msgs(&c, &CaptureController::status);
        QSignalSpy saved(&c, &CaptureController::snapshotSaved);

        const auto stallMessages = [&msgs] {
            int n = 0;
            for (const auto& call : msgs)
                if (call.at(0).toString().contains(QStringLiteral("Reconnecting"))) ++n;
            return n;
        };

        c.begin();
        src.emitOneFrame();      // starts the stall timer
        // then nothing -- the stall watchdog must fire
        QTRY_VERIFY_WITH_TIMEOUT(stallMessages() == 1,
                                 CaptureController::StallTimeoutMs + 3000);

        // THE REOPEN. The source must be running again, so a frame gets
        // through and the controller acts on it.
        src.emitOneFrame();
        c.takeSnapshot();        // refused outright if no frame has arrived
        src.emitOneFrame();      // the armed frame
        QTRY_VERIFY_WITH_TIMEOUT(saved.count() == 1, 10000);
        QVERIFY(QFileInfo(saved.at(0).at(0).toString()).size() > 0);

        // ... and the stall watchdog is armed again, so a second stall is
        // caught rather than the stream silently freezing for good.
        QTRY_VERIFY_WITH_TIMEOUT(stallMessages() == 2,
                                 CaptureController::StallTimeoutMs + 3000);
    }

    // The "then surface" half of the same spec row. If the reopen delivers
    // nothing either, the first-frame watchdog the stall handler re-arms
    // must fire and surface the failure -- otherwise the app sits on a
    // permanently dead stream having announced a reconnection that never
    // happened.
    //
    // Sensitivity: delete m_firstFrameTimer.start() from the stall handler
    // and this fails -- formatsExhausted never comes. (It is insensitive to
    // m_source->start(), which is why the test above exists as well: one
    // test per statement being deleted.)
    void aReopenThatDeliversNothingSurfacesViaTheFirstFrameWatchdog() {
        FakeCaptureSource src; src.setFrameSize({320, 240});
        src.setFallbackCount(0);   // nothing left to downgrade to
        QtRecorder rec; SnapshotWriter writer;
        CaptureController c(&src, &rec, &writer, m_dir.path());

        QSignalSpy exhausted(&c, &CaptureController::formatsExhausted);
        QSignalSpy msgs(&c, &CaptureController::status);

        c.begin();
        src.emitOneFrame();            // cancels the first-frame watchdog,
                                       // arms the stall watchdog
        src.setDeliverFrames(false);   // and now the stream is dead for good

        QTRY_VERIFY_WITH_TIMEOUT(exhausted.count() == 1,
                                 CaptureController::StallTimeoutMs
                                     + CaptureController::FirstFrameTimeoutMs + 5000);
        QVERIFY(msgs.count() > 0);
        QVERIFY(msgs.last().at(0).toString()
                    .contains(QStringLiteral("No video received from the scope")));
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

    // Review round on this task, Important 2: a driver Error that cuts a
    // recording short used to be invisible to m_pendingInterruption (only
    // Detach and a stall were tracked), so the deferred handler took the
    // plain "Recording saved as %1." branch -- discarding the driver's
    // detail entirely and never emitting sourceLost, which left AppContext
    // never tearing down and reopening a now-dead pipeline. This pins both
    // halves of the fix: the wording says "stopped" (not "disconnected",
    // since the scope was never unplugged) and names the cause, and
    // sourceLost actually fires so the app can recover.
    void errorDuringRecordingNamesTheCauseAndEndsTheTake() {
        FakeCaptureSource src; src.setFrameSize({320, 240});
        QtRecorder rec; SnapshotWriter writer;
        CaptureController c(&src, &rec, &writer, m_dir.path());

        QSignalSpy savedRec(&c, &CaptureController::recordingSaved);
        QSignalSpy lost(&c, &CaptureController::sourceLost);

        c.begin();
        c.startRecording();
        for (int i = 0; i < 45; ++i) { src.emitOneFrame(); QTest::qWait(16); }

        emit src.stopped(StopReason::Error, QStringLiteral("device in use by OtherApp"));

        QVERIFY(savedRec.wait(15000));
        QCOMPARE(lost.count(), 1);
        const QString msg = lost.at(0).at(0).toString();
        QVERIFY(msg.contains(QStringLiteral("stopped")));
        QVERIFY(!msg.contains(QStringLiteral("disconnected")));
        QVERIFY(msg.contains(QStringLiteral("OtherApp")));
        QVERIFY(!c.isRecording());
    }

    // Important 2, the no-false-positive half. The Linux EACCES diagnosis
    // now runs on the Error path (where a denial actually surfaces) against
    // the node the device really enumerated as -- not on the unreachable
    // `!start()` path against a hardcoded /dev/video0. A node that is
    // simply absent must not produce a permissions lecture; the technician
    // gets the real cause instead.
    //
    // Sensitivity: drop the `nodeExists` term from
    // StopClassification::isDeviceNodePermissionProblem() and this fails,
    // because QFileInfo("/dev/video7").isReadable() is false for a path
    // that does not exist -- which is every path, on this platform.
    void anAbsentDeviceNodeDoesNotProduceAPermissionsLecture() {
        FakeCaptureSource src;
        src.setDeviceNode(QStringLiteral("/dev/video7"));
        QtRecorder rec; SnapshotWriter writer;
        CaptureController c(&src, &rec, &writer, m_dir.path());

        QSignalSpy lost(&c, &CaptureController::sourceLost);
        c.begin();
        src.emitOneFrame();
        emit src.stopped(StopReason::Error, QStringLiteral("device in use by OtherApp"));

        QTRY_COMPARE_WITH_TIMEOUT(lost.count(), 1, 3000);
        const QString msg = lost.at(0).at(0).toString();
        QVERIFY2(!msg.contains(QStringLiteral("usermod")), qPrintable(msg));
        QVERIFY(msg.contains(QStringLiteral("OtherApp")));
        QCOMPARE(lost.at(0).at(1).value<StopReason>(), StopReason::Error);
    }
};

QTEST_MAIN(TestFailureModes)
#include "test_failure_modes.moc"
