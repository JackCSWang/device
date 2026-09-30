#include <QtTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include "capture/CaptureController.h"
#include "core/FakeCaptureSource.h"
#include "output/IRecorder.h"
#include "output/QtRecorder.h"
#include "output/SnapshotWriter.h"

// Minimal IRecorder test double used only by
// stopRecordingWhenNotRecordingNeverReachesTheRecorder(): QtRecorder's own
// finalizeAndStop() early-returns whenever !m_recording, which is
// defense-in-depth that would make that test pass even if
// CaptureController::stopRecording() lost its own "not recording, do
// nothing" guard. This fake has no such protection, so it is the one thing
// in this file that can actually distinguish "the guard ran" from
// "the guard was removed".
class CountingRecorder : public IRecorder {
public:
    bool start(const QString&, const QSize&, qreal) override { return true; }
    void feed(const QVideoFrame&, qint64) override {}
    void finalizeAndStop() override { ++finalizeCalls; }
    bool isRecording() const override { return false; }
    int finalizeCalls = 0;
};

class TestCaptureController : public QObject {
    Q_OBJECT
    QTemporaryDir m_dir;

private slots:
    void snapshotWritesAFileAndReportsIt() {
        FakeCaptureSource src; src.setFrameSize({640, 480});
        QtRecorder rec; SnapshotWriter writer;
        CaptureController c(&src, &rec, &writer, m_dir.path());

        QSignalSpy saved(&c, &CaptureController::snapshotSaved);
        c.begin();
        src.emitOneFrame();
        c.takeSnapshot();
        src.emitOneFrame();

        QVERIFY(saved.wait(5000));
        const QString path = saved.at(0).at(0).toString();
        QVERIFY(QFileInfo(path).size() > 0);
        QVERIFY(path.endsWith(QStringLiteral(".jpg")));
    }

    // Spec 10.1: the defining field failure.
    void detachMidRecordingFinalizesAndNamesTheFile() {
        FakeCaptureSource src; src.setFrameSize({640, 480});
        QtRecorder rec; SnapshotWriter writer;
        CaptureController c(&src, &rec, &writer, m_dir.path());

        QSignalSpy savedRec(&c, &CaptureController::recordingSaved);
        QSignalSpy lost(&c, &CaptureController::sourceLost);

        c.begin();
        c.startRecording();
        for (int i = 0; i < 45; ++i) { src.emitOneFrame(); QTest::qWait(16); }

        src.injectDetach();

        QVERIFY(savedRec.wait(15000));
        const QString path = savedRec.at(0).at(0).toString();
        QVERIFY2(QFileInfo(path).size() > 1024, "recording was not finalized");
        QCOMPARE(lost.count(), 1);
        // The message must name the saved file, or the technician assumes
        // the take was lost.
        QVERIFY(lost.at(0).at(0).toString().contains(QFileInfo(path).fileName()));
        QVERIFY(!c.isRecording());
    }

    // Spec 10.1, the other half: a detach in the window between pressing
    // Record and the first frame is exactly this task's field scenario (a
    // flaky connection), and QtRecorder deletes the zero-frame file and
    // reports failed(), never finished(). sourceLost must not claim the
    // file was saved when it was in fact just deleted.
    void detachBeforeAnyFrameDoesNotClaimTheRecordingWasSaved() {
        FakeCaptureSource src; src.setFrameSize({640, 480});
        QtRecorder rec; SnapshotWriter writer;
        CaptureController c(&src, &rec, &writer, m_dir.path());

        QSignalSpy savedRec(&c, &CaptureController::recordingSaved);
        QSignalSpy lost(&c, &CaptureController::sourceLost);

        c.begin();
        c.startRecording();
        src.injectDetach();   // no frame was ever fed

        QVERIFY(lost.wait(15000));
        QCOMPARE(savedRec.count(), 0);
        const QString message = lost.at(0).at(0).toString();
        QVERIFY(!message.contains(QStringLiteral("was saved as")));
        QVERIFY(message.contains(QStringLiteral("could not be saved")));
        QVERIFY(!c.isRecording());
    }

    // Spec 10.1 again: a stall is not a detach, but it still cuts a
    // recording short. The technician must be told the take ended and will
    // not resume on its own -- and, since the scope itself reconnects
    // automatically here, this is not a sourceLost.
    void stallDuringRecordingFinalizesAndSaysSo() {
        FakeCaptureSource src; src.setFrameSize({640, 480});
        QtRecorder rec; SnapshotWriter writer;
        CaptureController c(&src, &rec, &writer, m_dir.path());

        QSignalSpy savedRec(&c, &CaptureController::recordingSaved);
        QSignalSpy lost(&c, &CaptureController::sourceLost);
        QSignalSpy msgs(&c, &CaptureController::status);

        c.begin();
        c.startRecording();
        for (int i = 0; i < 10; ++i) { src.emitOneFrame(); QTest::qWait(16); }
        // Then go silent long enough to trip the stall watchdog.

        QVERIFY(savedRec.wait(CaptureController::StallTimeoutMs + 15000));
        const QString path = savedRec.at(0).at(0).toString();
        QVERIFY2(QFileInfo(path).size() > 1024, "recording was not finalized");
        QCOMPARE(lost.count(), 0);   // a stall reconnects; it did not lose the scope

        bool sawInterruptionMessage = false;
        for (const auto& call : msgs) {
            if (call.at(0).toString().contains(QStringLiteral("will not resume automatically")))
                sawInterruptionMessage = true;
        }
        QVERIFY(sawInterruptionMessage);
        QVERIFY(!c.isRecording());   // ended, and not silently replaced by a new one
    }

    // Review Focus 5: both at once, neither starving the other.
    void snapshotWhileRecordingSucceedsForBoth() {
        FakeCaptureSource src; src.setFrameSize({640, 480});
        QtRecorder rec; SnapshotWriter writer;
        CaptureController c(&src, &rec, &writer, m_dir.path());

        QSignalSpy snap(&c, &CaptureController::snapshotSaved);
        QSignalSpy savedRec(&c, &CaptureController::recordingSaved);

        c.begin();
        c.startRecording();
        for (int i = 0; i < 20; ++i) { src.emitOneFrame(); QTest::qWait(16); }
        c.takeSnapshot();
        for (int i = 0; i < 20; ++i) { src.emitOneFrame(); QTest::qWait(16); }
        c.stopRecording();

        // Not QVERIFY(snap.wait(5000)): the preceding qWait loops already
        // pump the event loop, so the snapshot's written() signal is very
        // likely delivered before we ever reach this line. QSignalSpy::wait()
        // only counts emissions strictly after it starts waiting -- it
        // records the spy's count at entry and blocks for an *additional*
        // one -- so a spy that already has its one-and-only entry would wait
        // out the full timeout for a second emission that never comes. Guard
        // with the spy's own count first, exactly like
        // twoSnapshotsInTheSameSecondBothSurvive() already does with
        // QTRY_COMPARE_WITH_TIMEOUT below.
        QTRY_VERIFY_WITH_TIMEOUT(snap.count() >= 1, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(savedRec.count() >= 1, 15000);
        QVERIFY(QFileInfo(snap.at(0).at(0).toString()).size() > 0);
        QVERIFY(QFileInfo(savedRec.at(0).at(0).toString()).size() > 1024);
    }

    // Spec 10.2: open() succeeding is not evidence that streaming works.
    void noFramesAfterOpenTriggersTheWatchdog() {
        FakeCaptureSource src;
        src.setDeliverFrames(false);
        QtRecorder rec; SnapshotWriter writer;
        CaptureController c(&src, &rec, &writer, m_dir.path());

        QSignalSpy timedOut(&c, &CaptureController::firstFrameTimedOut);
        c.begin();
        QVERIFY(timedOut.wait(CaptureController::FirstFrameTimeoutMs + 2000));
    }

    void framesArrivingCancelTheFirstFrameWatchdog() {
        FakeCaptureSource src;
        QtRecorder rec; SnapshotWriter writer;
        CaptureController c(&src, &rec, &writer, m_dir.path());

        QSignalSpy timedOut(&c, &CaptureController::firstFrameTimedOut);
        c.begin();
        src.emitOneFrame();
        QTest::qWait(CaptureController::FirstFrameTimeoutMs + 500);
        QCOMPARE(timedOut.count(), 0);
    }

    void snapshotIsRefusedWhenNoFrameHasArrived() {
        FakeCaptureSource src;
        src.setDeliverFrames(false);
        QtRecorder rec; SnapshotWriter writer;
        CaptureController c(&src, &rec, &writer, m_dir.path());

        QSignalSpy saved(&c, &CaptureController::snapshotSaved);
        QSignalSpy msgs(&c, &CaptureController::status);
        c.begin();
        c.takeSnapshot();
        QTest::qWait(500);
        QCOMPARE(saved.count(), 0);
        QVERIFY(msgs.count() > 0);
    }

    void twoSnapshotsInTheSameSecondBothSurvive() {
        FakeCaptureSource src; src.setFrameSize({320, 240});
        QtRecorder rec; SnapshotWriter writer;
        CaptureController c(&src, &rec, &writer, m_dir.path());

        QSignalSpy saved(&c, &CaptureController::snapshotSaved);
        c.begin();
        src.emitOneFrame();
        c.takeSnapshot(); src.emitOneFrame();
        c.takeSnapshot(); src.emitOneFrame();

        QTRY_COMPARE_WITH_TIMEOUT(saved.count(), 2, 5000);
        const QString a = saved.at(0).at(0).toString();
        const QString b = saved.at(1).at(0).toString();
        QVERIFY(a != b);
        QVERIFY(QFileInfo(a).size() > 0);
        QVERIFY(QFileInfo(b).size() > 0);
    }

    void stopRecordingWhenNotRecordingIsHarmless() {
        FakeCaptureSource src;
        QtRecorder rec; SnapshotWriter writer;
        CaptureController c(&src, &rec, &writer, m_dir.path());

        QSignalSpy savedRec(&c, &CaptureController::recordingSaved);
        QSignalSpy msgs(&c, &CaptureController::status);
        // m_dir is shared across every test function in this class, so
        // earlier tests' .mp4 files are already sitting in it -- compare
        // the count before and after, not against zero.
        const int mp4CountBefore =
            QDir(m_dir.path()).entryList({QStringLiteral("*.mp4")}, QDir::Files).size();

        c.begin();
        c.stopRecording();
        QTest::qWait(200);

        QVERIFY(!c.isRecording());
        QCOMPARE(savedRec.count(), 0);
        QCOMPARE(msgs.count(), 0);
        QCOMPARE(QDir(m_dir.path()).entryList({QStringLiteral("*.mp4")}, QDir::Files).size(),
                 mp4CountBefore);
    }

    // stopRecordingWhenNotRecordingIsHarmless() above is insensitive to
    // CaptureController::stopRecording()'s own "not recording, do nothing"
    // guard: QtRecorder::finalizeAndStop() early-returns on !m_recording
    // regardless, so that test would pass identically with the guard
    // removed. This test isolates the guard with a bare IRecorder double
    // that has no such protection of its own, so it genuinely fails if the
    // guard is removed (verified by hand: see task-9-report.md's Important
    // 3 section for the remove/restore transcript).
    void stopRecordingWhenNotRecordingNeverReachesTheRecorder() {
        FakeCaptureSource src;
        CountingRecorder rec; SnapshotWriter writer;
        CaptureController c(&src, &rec, &writer, m_dir.path());

        c.begin();
        c.stopRecording();

        QCOMPARE(rec.finalizeCalls, 0);
        QVERIFY(!c.isRecording());
    }
};

QTEST_MAIN(TestCaptureController)
#include "test_capture_controller.moc"
