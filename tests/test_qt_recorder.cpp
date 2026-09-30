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
};

QTEST_MAIN(TestQtRecorder)
#include "test_qt_recorder.moc"
