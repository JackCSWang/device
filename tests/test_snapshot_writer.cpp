#include <QtTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QImageReader>
#include "core/FakeCaptureSource.h"
#include "output/SnapshotWriter.h"

class TestSnapshotWriter : public QObject {
    Q_OBJECT
    QTemporaryDir m_dir;

    QString path(const QString& name) { return m_dir.filePath(name); }

private slots:
    void writesFullResolutionJpeg() {
        FakeCaptureSource src;
        src.setFrameSize({1920, 1080});
        src.start();

        SnapshotWriter writer;
        QSignalSpy spy(&writer, &SnapshotWriter::written);
        const QString out = path(QStringLiteral("a.jpg"));

        connect(&src, &ICaptureSource::frameReady, this,
                [&](const QVideoFrame& f, qint64 ts) { writer.write(f, ts, out); });
        src.emitOneFrame();

        QVERIFY(spy.wait(5000));
        QCOMPARE(spy.at(0).at(1).toSize(), QSize(1920, 1080));

        QImageReader reader(out);
        QCOMPARE(reader.format(), QByteArray("jpeg"));
        QCOMPARE(reader.size(), QSize(1920, 1080));
    }

    // Review Focus 3: the spec's own preference list includes YUY2.
    void writesCorrectPixelsFromYuyvFrames() {
        FakeCaptureSource src;
        src.setFrameSize({320, 240});
        src.setPixelFormat(QVideoFrameFormat::Format_YUYV);
        src.start();

        SnapshotWriter writer;
        QSignalSpy spy(&writer, &SnapshotWriter::written);
        const QString out = path(QStringLiteral("yuyv.jpg"));
        const QColor expected = src.nextFillColor();

        connect(&src, &ICaptureSource::frameReady, this,
                [&](const QVideoFrame& f, qint64 ts) { writer.write(f, ts, out); });
        src.emitOneFrame();

        QVERIFY(spy.wait(5000));
        const QImage back(out);
        QVERIFY(!back.isNull());
        QCOMPARE(back.size(), QSize(320, 240));

        // The actual colour must survive YUV -> RGB -> JPEG. A format mix-up
        // yields a grey smear or a wildly wrong hue; asserting "not grey"
        // alone would pass on garbage, so assert the hue.
        const QColor got = back.pixelColor(160, 120);
        QVERIFY2(got.saturation() > 40, "colour was lost entirely");
        const int hueError = qAbs(got.hue() - expected.hue());
        QVERIFY2(qMin(hueError, 360 - hueError) < 20,
                 qPrintable(QStringLiteral("hue %1, expected %2")
                            .arg(got.hue()).arg(expected.hue())));
    }

    void survivesSourceRecyclingItsBuffer() {
        FakeCaptureSource src;
        src.setFrameSize({64, 48});
        src.setScribbleAfterEmit(true);
        src.start();

        SnapshotWriter writer;
        QSignalSpy spy(&writer, &SnapshotWriter::written);
        const QString out = path(QStringLiteral("race.jpg"));
        const QColor expected = src.nextFillColor();

        connect(&src, &ICaptureSource::frameReady, this,
                [&](const QVideoFrame& f, qint64 ts) { writer.write(f, ts, out); });
        src.emitOneFrame();

        QVERIFY(spy.wait(5000));
        const QImage back(out);
        QCOMPARE(back.pixelColor(32, 24).hue(), expected.hue());
    }

    void failsLoudlyOnUnwritablePath() {
        FakeCaptureSource src;
        src.start();
        SnapshotWriter writer;
        QSignalSpy failures(&writer, &SnapshotWriter::failed);

        connect(&src, &ICaptureSource::frameReady, this, [&](const QVideoFrame& f, qint64 ts) {
            writer.write(f, ts, m_dir.filePath(QStringLiteral("nope/deeper/x.jpg")));
        });
        src.emitOneFrame();

        QVERIFY(failures.wait(5000));
        QVERIFY(!failures.at(0).at(1).toString().isEmpty());
    }

    void invalidFrameFailsRatherThanWritingAnEmptyFile() {
        SnapshotWriter writer;
        QSignalSpy failures(&writer, &SnapshotWriter::failed);
        const QString out = path(QStringLiteral("invalid.jpg"));
        writer.write(QVideoFrame(), 0, out);
        QVERIFY(failures.wait(5000));
        QVERIFY(!QFile::exists(out));
    }
};

QTEST_MAIN(TestSnapshotWriter)
#include "test_snapshot_writer.moc"
