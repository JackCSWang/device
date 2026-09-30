#include <QtTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QMediaPlayer>
#include "core/FakeCaptureSource.h"
#include "output/QtRecorder.h"

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
};

QTEST_MAIN(TestQtRecorder)
#include "test_qt_recorder.moc"
