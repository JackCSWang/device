#include <QtTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include "capture/CaptureController.h"
#include "core/FakeCaptureSource.h"
#include "output/QtRecorder.h"
#include "output/SnapshotWriter.h"

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
        c.begin();
        c.stopRecording();
        QVERIFY(!c.isRecording());
    }
};

QTEST_MAIN(TestCaptureController)
#include "test_capture_controller.moc"
