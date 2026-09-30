#include <QtTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include "capture/CaptureController.h"
#include "core/FakeCaptureSource.h"
#include "output/IRecorder.h"
#include "output/QtRecorder.h"
#include "output/SnapshotWriter.h"
#include <cstddef>
#include <cstring>
#include <new>

// A scriptable IRecorder test double, used by three tests below to probe
// CaptureController's own logic in isolation from a real recorder's
// behaviour and timing:
//
//  - stopRecordingWhenNotRecordingNeverReachesTheRecorder(): QtRecorder's
//    own finalizeAndStop() early-returns whenever !m_recording, which is
//    defense-in-depth that would make that test pass even if
//    CaptureController::stopRecording() lost its own "not recording, do
//    nothing" guard. This double has no such protection, so it is the one
//    thing in this file that can actually distinguish "the guard ran" from
//    "the guard was removed".
//  - errorThenDetachProducesAMessageAndNeverClaimsTheFileWasSaved(): real
//    QtRecorder error timing cannot be forced deterministically without
//    also forcing an explicit stop (see test_qt_recorder.cpp's own
//    Windows-lock workaround for that), so simulateMidRecordingError()
//    exercises the IRecorder contract (see IRecorder.h) directly: an error
//    latch must make isRecording() false atomically with failed() firing.
//  - startRecordingClearsAStrandedInterruptionFlag(): needs a window,
//    reliably held open, in which a detach/stall's outcome has not yet
//    arrived. Unlike the real QtRecorder, which this round's fix makes
//    always emit exactly one of finished()/failed() for a recording that
//    was genuinely in flight, this double's finalizeAndStop() does not
//    resolve anything on its own -- callers resolve it explicitly via
//    simulateFinished()/simulateMidRecordingError(), or leave it open.
class ScriptedRecorder : public IRecorder {
public:
    bool start(const QString&, const QSize&, qreal) override {
        m_recording = true;
        return true;
    }
    void feed(const QVideoFrame&, qint64) override {}
    void finalizeAndStop() override { m_recording = false; ++finalizeCalls; }
    bool isRecording() const override { return m_recording; }

    void simulateMidRecordingError(const QString& reason) {
        m_recording = false;
        emit failed(QString(), reason);
    }

    void simulateFinished(const QString& path, qint64 durationUs = 1000000) {
        emit finished(path, durationUs);
    }

    int finalizeCalls = 0;

private:
    bool m_recording = false;
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
        ScriptedRecorder rec; SnapshotWriter writer;
        CaptureController c(&src, &rec, &writer, m_dir.path());

        c.begin();
        c.stopRecording();

        QCOMPARE(rec.finalizeCalls, 0);
        QVERIFY(!c.isRecording());
    }

    // New Important A (2nd review round): once a recording has failed,
    // isRecording() must say so immediately, per IRecorder's contract --
    // otherwise a detach that follows finds isRecording() still true on a
    // dead recorder, takes the deferred-interruption path, and waits
    // forever for an outcome that was already reported and will never come
    // again: total silence rather than a wrong message. Deterministic via
    // ScriptedRecorder (see its comment above for why this is the right
    // seam to test at, rather than trying to force a real QtRecorder error
    // without an explicit stop).
    void errorThenDetachProducesAMessageAndNeverClaimsTheFileWasSaved() {
        FakeCaptureSource src; src.setFrameSize({640, 480});
        ScriptedRecorder rec; SnapshotWriter writer;
        CaptureController c(&src, &rec, &writer, m_dir.path());

        QSignalSpy savedRec(&c, &CaptureController::recordingSaved);
        QSignalSpy lost(&c, &CaptureController::sourceLost);
        QSignalSpy msgs(&c, &CaptureController::status);

        c.begin();
        c.startRecording();
        const int msgsBeforeError = msgs.count();   // already has "Recording to ..."
        src.emitOneFrame();
        QVERIFY(c.isRecording());

        rec.simulateMidRecordingError(
            QStringLiteral("Recording failed: disk write error. The file may be incomplete."));

        // Honest immediately -- not "whenever finalizeAndStop() next runs".
        QVERIFY(!c.isRecording());
        QVERIFY2(msgs.count() > msgsBeforeError,
                 "the error itself must be reported, not silence");

        src.injectDetach();

        // Synchronous with injectDetach(): no wait() needed (and none of
        // the spies here have had a chance to accumulate a stale count
        // that a wait() would then have to see *another* emission past --
        // see snapshotWhileRecordingSucceedsForBoth()'s comment for that
        // QSignalSpy::wait() pitfall).
        QCOMPARE(lost.count(), 1);   // isRecording() being honest means this
                                     // is the immediate "nothing was being
                                     // recorded" branch, not a silent, still-
                                     // pending Detach interruption.
        QCOMPARE(savedRec.count(), 0);
        for (const auto& call : msgs)
            QVERIFY(!call.at(0).toString().contains(QStringLiteral("was saved as")));
    }

    // New Important B (2nd review round): m_pendingInterruption must not
    // survive past the recording it was set for. ScriptedRecorder's
    // finalizeAndStop() deliberately does not resolve the outcome on its
    // own, holding the "still pending" window open long enough to prove
    // startRecording() clears it for the next, unrelated recording.
    void startRecordingClearsAStrandedInterruptionFlag() {
        FakeCaptureSource src; src.setFrameSize({640, 480});
        ScriptedRecorder rec; SnapshotWriter writer;
        CaptureController c(&src, &rec, &writer, m_dir.path());

        QSignalSpy lost(&c, &CaptureController::sourceLost);
        QSignalSpy savedRec(&c, &CaptureController::recordingSaved);
        QSignalSpy msgs(&c, &CaptureController::status);

        c.begin();
        c.startRecording();
        src.emitOneFrame();
        src.injectDetach();
        // wasRecording was true, so onSourceStopped set
        // m_pendingInterruption to Detach and called finalizeAndStop() --
        // whose outcome, by this double's design, never arrives on its
        // own. The message is deferred, waiting.
        QCOMPARE(lost.count(), 0);
        QVERIFY(!c.isRecording());

        // Reconnect and start a second, completely unrelated recording.
        c.begin();
        c.startRecording();
        const QString newPath = m_dir.filePath(QStringLiteral("healthy-take.mp4"));
        rec.simulateFinished(newPath);

        // This recording's own completion must be reported plainly, not
        // hijacked by the stale Detach flag from the earlier, already-gone
        // recording.
        QCOMPARE(savedRec.count(), 1);
        QCOMPARE(savedRec.at(0).at(0).toString(), newPath);
        QCOMPARE(lost.count(), 0);
        for (const auto& call : msgs)
            QVERIFY(!call.at(0).toString().contains(QStringLiteral("disconnected")));
    }

    // Review round 3, Critical C1: a real consumer's sourceLost handler may
    // delete the controller synchronously -- AppContext's own did, until it
    // was changed to defer its teardown off the emitting stack (see
    // task-12-report.md). Regardless of what any particular consumer does,
    // CaptureController itself must never touch `this` after the sourceLost
    // emit that a synchronous deleter reacts to: the old code followed that
    // emit with a plain member access on freed memory when nothing was
    // recording (Path A), and, when a recording was in flight, emitted
    // sourceLost from inside the recorder's own finished()/failed() handler
    // -- deleting the recorder while its own recorderStateChanged emission
    // was still unwinding beneath it (Path B). Both are plain
    // use-after-free, not something Qt's connection-list reentrancy safety
    // covers. These two tests build a consumer that does exactly what the
    // old AppContext did -- destroy the controller synchronously, from
    // inside the sourceLost handler itself -- and confirm the controller
    // survives it, with and without a recording in flight.
    //
    // The controller is placement-constructed into a test-owned buffer
    // rather than heap-allocated with plain `new`, and the sourceLost
    // handler calls its destructor explicitly and poisons the buffer,
    // rather than `delete c`. A first version of these two tests used a
    // global `operator delete` override that poisoned (and, to stay
    // sensitive, deliberately leaked) every freed block up to 64KiB across
    // the whole binary. Review round 4 correctly rejected it: its
    // sensitivity depended entirely on -fsized-deallocation being enabled
    // (the *sized* overload is the one that poisons -- if that flag were
    // ever off, `delete c` would route to the unsized overload, call plain
    // free(), and both tests would pass with the use-after-free still live,
    // silently), it pre-empted libstdc++ for this whole test binary rather
    // than being scoped to these two tests, it made a genuine double-free
    // in this binary undetectable, and it made this binary unusable under
    // any heap or leak tool. Placement-new has none of those costs: no
    // global operator is replaced, nothing is leaked, poisoning happens
    // unconditionally (no compiler flag it can silently ride on), and it
    // stays scoped to exactly these two tests.
    void deletingTheControllerInsideSourceLostSurvivesWithNoRecordingInFlight() {
        FakeCaptureSource src; src.setFrameSize({640, 480});
        QtRecorder rec; SnapshotWriter writer;
        alignas(CaptureController) std::byte storage[sizeof(CaptureController)];
        auto* c = new (storage) CaptureController(&src, &rec, &writer, m_dir.path());

        // A real consumer (AppContext) connects status() too, not just
        // sourceLost(). Without a genuine listener here, status()'s
        // emit -- the very statement that follows the deleting sourceLost
        // emit on this path -- can degenerate into "sender has no
        // connections for this signal, return", which does not exercise
        // the same freed connection-list data a real dispatch would.
        // Recording both signals' arrival order, rather than just counting
        // status(), also pins the ruled invariant directly: sourceLost must
        // be the last thing that happens, not merely inferred from the
        // absence of a crash.
        QStringList order;
        QObject::connect(c, &CaptureController::status, c,
                          [&](const QString&) { order << QStringLiteral("status"); });
        bool deleted = false;
        QObject::connect(c, &CaptureController::sourceLost, c, [&] {
            order << QStringLiteral("sourceLost");
            c->~CaptureController();
            std::memset(storage, 0xDE, sizeof storage);
            deleted = true;
        });

        c->begin();
        src.emitOneFrame();
        src.injectDetach();   // synchronous: onSourceStopped -> sourceLost -> destroy c

        QVERIFY(deleted);
        QCOMPARE(order, QStringList({QStringLiteral("status"), QStringLiteral("sourceLost")}));
    }

    void deletingTheControllerInsideSourceLostSurvivesWithARecordingInFlight() {
        FakeCaptureSource src; src.setFrameSize({640, 480});
        QtRecorder rec; SnapshotWriter writer;
        alignas(CaptureController) std::byte storage[sizeof(CaptureController)];
        auto* c = new (storage) CaptureController(&src, &rec, &writer, m_dir.path());

        // See the matching comment in the no-recording variant above.
        QStringList order;
        QObject::connect(c, &CaptureController::status, c,
                          [&](const QString&) { order << QStringLiteral("status"); });
        bool deleted = false;
        QObject::connect(c, &CaptureController::sourceLost, c, [&] {
            order << QStringLiteral("sourceLost");
            c->~CaptureController();
            std::memset(storage, 0xDE, sizeof storage);
            deleted = true;
        });

        c->begin();
        c->startRecording();
        QVERIFY(c->isRecording());   // pin that a recording genuinely began;
                                      // otherwise a silent startRecording()
                                      // failure would degrade this into a
                                      // second copy of the no-recording test
                                      // above and still pass.
        for (int i = 0; i < 45; ++i) { src.emitOneFrame(); QTest::qWait(16); }

        // Discard startRecording()'s own "Recording to ..." status() before
        // capturing the detach-triggered sequence, so both tests assert the
        // identical, minimal order.
        order.clear();
        src.injectDetach();

        // finalizeAndStop()'s outcome can resolve asynchronously (real
        // QtRecorder, real encoder), so the destruction may land after
        // injectDetach() returns -- wait for it exactly like
        // detachMidRecordingFinalizesAndNamesTheFile() waits for
        // recordingSaved.
        QTRY_VERIFY_WITH_TIMEOUT(deleted, 15000);
        QCOMPARE(order, QStringList({QStringLiteral("status"), QStringLiteral("sourceLost")}));
    }
};

QTEST_MAIN(TestCaptureController)
#include "test_capture_controller.moc"
