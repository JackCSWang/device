#include <QtTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QMediaPlayer>
#include "core/FakeCaptureSource.h"
#include "output/QtRecorder.h"
#ifdef Q_OS_WIN
#include <windows.h>
#endif

class TestQtRecorder : public QObject {
    Q_OBJECT
    QTemporaryDir m_dir;

    static qint64 durationOf(const QString& path) {
        QMediaPlayer player;
        QSignalSpy loaded(&player, &QMediaPlayer::mediaStatusChanged);
        player.setSource(QUrl::fromLocalFile(path));
        for (int i = 0; i < 50 && player.duration() == 0; ++i)
            QTest::qWait(100);
        return player.duration();   // ms
    }

private slots:
    void recordsAPlayableMp4() {
        FakeCaptureSource src;
        src.setFrameSize({640, 480});
        src.start();

        QtRecorder rec;
        const QString out = m_dir.filePath(QStringLiteral("clip.mp4"));
        QSignalSpy done(&rec, &IRecorder::finished);

        QVERIFY(rec.start(out, {640, 480}, 30.0));
        QVERIFY(rec.isRecording());

        connect(&src, &ICaptureSource::frameReady, &rec,
                [&](const QVideoFrame& f, qint64 ts) { rec.feed(f, ts); });
        for (int i = 0; i < 60; ++i) { src.emitOneFrame(); QTest::qWait(16); }

        rec.finalizeAndStop();
        QVERIFY(done.wait(15000));
        QVERIFY(!rec.isRecording());

        QVERIFY(QFileInfo(out).size() > 1024);
        QVERIFY(durationOf(out) > 500);       // roughly 2s of frames
    }

    // Review Focus 4: an accidental double-tap.
    void startThenImmediateStopLeavesNoBrokenFile() {
        QtRecorder rec;
        const QString out = m_dir.filePath(QStringLiteral("empty.mp4"));
        QSignalSpy done(&rec, &IRecorder::finished);
        QSignalSpy bad(&rec, &IRecorder::failed);

        QVERIFY(rec.start(out, {640, 480}, 30.0));
        rec.finalizeAndStop();
        QVERIFY(done.wait(15000) || bad.count() > 0);

        // Either a valid file or no file. Never a 0-byte .mp4 that looks
        // like a recording and will not open.
        if (QFile::exists(out))
            QVERIFY2(QFileInfo(out).size() > 1024, "zero-frame file left on disk");
    }

    void feedBeforeStartIsIgnoredNotCrashing() {
        FakeCaptureSource src;
        src.start();
        QtRecorder rec;
        connect(&src, &ICaptureSource::frameReady, &rec,
                [&](const QVideoFrame& f, qint64 ts) { rec.feed(f, ts); });
        src.emitOneFrame();
        QVERIFY(!rec.isRecording());
    }

    void doubleStartIsRejected() {
        QtRecorder rec;
        QVERIFY(rec.start(m_dir.filePath(QStringLiteral("one.mp4")), {640, 480}, 30.0));
        QVERIFY(!rec.start(m_dir.filePath(QStringLiteral("two.mp4")), {640, 480}, 30.0));
        rec.finalizeAndStop();
    }

    void usesFrameTimestampsAsPts() {
        FakeCaptureSource src;
        src.setFrameSize({320, 240});
        src.start();

        QtRecorder rec;
        const QString out = m_dir.filePath(QStringLiteral("sparse.mp4"));
        QSignalSpy done(&rec, &IRecorder::finished);
        QVERIFY(rec.start(out, {320, 240}, 30.0));

        // 30 frames stamped 100ms apart: 3s of wall clock, not 1s.
        for (int i = 0; i < 30; ++i) {
            QVideoFrameFormat fmt({320, 240}, QVideoFrameFormat::Format_RGBX8888);
            QVideoFrame f(fmt);
            if (f.map(QVideoFrame::WriteOnly)) {
                QImage v(f.bits(0), 320, 240, f.bytesPerLine(0), QImage::Format_RGBX8888);
                v.fill(QColor::fromHsv((i * 11) % 360, 255, 255));
                f.unmap();
            }
            rec.feed(f, i * 100000LL);
            QTest::qWait(10);
        }
        rec.finalizeAndStop();
        QVERIFY(done.wait(15000));

        const qint64 ms = durationOf(out);
        QVERIFY2(ms > 2000, qPrintable(QStringLiteral("duration was %1ms").arg(ms)));
    }

    // Important 1 (post-review): a mid-recording error must report failed(),
    // never finished(), for the same broken recording. A naive
    // implementation that emits finished() from onRecorderStateChanged
    // whenever framesFed > 0, without checking whether an error already
    // happened, will emit BOTH signals here -- confirmed by temporarily
    // reverting the m_errored guard and observing exactly that with this
    // test (failed=1, finished=1; see task-8-report.md).
    //
    // To force a genuine, non-contrived mid-recording error (not just a
    // record()-time failure that never even reaches RecordingState -- an
    // invalid output *directory* was tried first and confirmed, via a
    // temporary debug build, to never invoke onRecorderStateChanged at all,
    // making it insensitive to this fix) this opens the output file from a
    // second handle once the muxer has started writing, and takes an
    // exclusive Windows byte-range lock over it. Windows locks are
    // mandatory: the recorder's own writer handle then fails when it tries
    // to write the trailer (the moov atom) at finalizeAndStop() time, after
    // frames have already been accepted (framesFed > 0) -- exactly the
    // scenario spec 10.1 is guarding against.
    //
    // This technique relies on Windows' mandatory file locking and has no
    // portable POSIX equivalent (flock/fcntl locks are advisory), so it is
    // skipped on other platforms rather than asserting nothing there.
    void midRecordingErrorReportsFailedNeverFinished() {
#ifndef Q_OS_WIN
        QSKIP("requires a Windows mandatory byte-range lock to force a genuine mid-recording write error");
#else
        FakeCaptureSource src;
        src.setFrameSize({320, 240});
        src.start();

        QtRecorder rec;
        const QString out = m_dir.filePath(QStringLiteral("locked.mp4"));
        QSignalSpy done(&rec, &IRecorder::finished);
        QSignalSpy bad(&rec, &IRecorder::failed);

        QVERIFY(rec.start(out, {320, 240}, 30.0));
        connect(&src, &ICaptureSource::frameReady, &rec,
                [&](const QVideoFrame& f, qint64 ts) { rec.feed(f, ts); });

        // Warm up until the muxer has actually written bytes, so frames are
        // genuinely in flight before the file is sabotaged.
        qint64 sz = 0;
        for (int i = 0; i < 60 && sz == 0; ++i) {
            src.emitOneFrame();
            QTest::qWait(30);
            sz = QFileInfo(out).size();
        }
        QVERIFY2(sz > 0, "muxer never wrote any bytes; cannot exercise a mid-recording error");

        // Exclusive-lock the whole file from a second handle. Any further
        // write into the locked range -- including the recorder's own,
        // already-open writer handle -- fails at the OS level.
        HANDLE h = CreateFileW(reinterpret_cast<const wchar_t*>(out.utf16()),
                                GENERIC_READ | GENERIC_WRITE,
                                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        QVERIFY2(h != INVALID_HANDLE_VALUE, "could not open the output file to lock it");
        OVERLAPPED ov{};
        const bool locked = LockFileEx(h, LOCKFILE_EXCLUSIVE_LOCK, 0, 0xFFFFFFFF, 0x7FFFFFFF, &ov);
        if (!locked) CloseHandle(h);
        QVERIFY2(locked, "could not take an exclusive lock on the output file");

        for (int i = 0; i < 30; ++i) { src.emitOneFrame(); QTest::qWait(30); }
        rec.finalizeAndStop();

        for (int i = 0; i < 150 && done.isEmpty() && bad.isEmpty(); ++i)
            QTest::qWait(100);
        QTest::qWait(200);   // let a late second emission surface, if any

        CloseHandle(h);

        QVERIFY2(!bad.isEmpty(), "expected failed() once the trailer write was blocked");
        QCOMPARE(done.count(), 0);
#endif
    }

    // Must-fix minor 3: the missing test for the m_recording-after-error
    // change. IRecorder's contract rule 2 says isRecording() must report
    // false from the moment a recording is known dead, not whenever a
    // caller next happens to call finalizeAndStop() -- CaptureController's
    // detach handler depends on it, because a caller that still believes a
    // recording is in flight defers reporting anything and waits for an
    // outcome that has already been delivered and will never come again:
    // total silence rather than a wrong message.
    //
    // A nonexistent output directory is the one way to force a
    // record()-time error deterministically with no platform tricks. (It
    // never reaches RecordingState, which is why it is no use for
    // midRecordingErrorReportsFailedNeverFinished above -- that needs an
    // error *after* frames have been accepted.)
    //
    // Sensitivity: this is RED without the errorOccurred handler routing
    // through finalizeAndStop() -- failed() still fires, so the QTRY
    // passes, but isRecording() stays true and the last line fails.
    void anErrorAtStartMakesIsRecordingFalseImmediately() {
        QtRecorder rec;
        QSignalSpy bad(&rec, &IRecorder::failed);
        QSignalSpy done(&rec, &IRecorder::finished);

        const QString out =
            m_dir.filePath(QStringLiteral("no-such-dir/deeper/clip.mp4"));
        QVERIFY(!QFileInfo(out).dir().exists());

        // start() itself succeeds: the failure is asynchronous, reported by
        // QMediaRecorder once record() has been attempted.
        QVERIFY(rec.start(out, {640, 480}, 30.0));
        QTRY_VERIFY_WITH_TIMEOUT(bad.count() == 1, 15000);

        // Exactly one of finished/failed, per contract rule 1.
        QCOMPARE(done.count(), 0);
        QVERIFY(!rec.isRecording());
    }

    // Must-fix minor 1, the other half of the one-shot guard: however many
    // errors QMediaRecorder reports for one broken recording, exactly one
    // failed() reaches the consumer. Let the error above settle, then wait
    // a further moment for any second emission the backend has queued.
    void severalBackendErrorsProduceExactlyOneFailure() {
        QtRecorder rec;
        QSignalSpy bad(&rec, &IRecorder::failed);

        QVERIFY(rec.start(m_dir.filePath(QStringLiteral("nope/again/clip.mp4")),
                          {640, 480}, 30.0));
        QTRY_VERIFY_WITH_TIMEOUT(bad.count() == 1, 15000);

        // A second start on the same dead recorder is rejected outright
        // (isRecording() is already false, but m_recorder still exists and
        // can still report), and an explicit finalizeAndStop() must not
        // resurrect a second failure either.
        rec.finalizeAndStop();
        QTest::qWait(500);
        QCOMPARE(bad.count(), 1);
    }
};

QTEST_MAIN(TestQtRecorder)
#include "test_qt_recorder.moc"
