#include <QtTest>
#include <QSignalSpy>
#include "core/FakeCaptureSource.h"
#include "core/Frame.h"

class TestFakeCaptureSource : public QObject {
    Q_OBJECT
private slots:
    void emitsFramesWithMonotonicTimestamps() {
        FakeCaptureSource src;
        src.setFrameSize({640, 480});
        QVERIFY(src.start());
        QSignalSpy spy(&src, &ICaptureSource::frameReady);
        src.emitOneFrame();
        src.emitOneFrame();
        QCOMPARE(spy.count(), 2);
        const qint64 t0 = spy.at(0).at(1).toLongLong();
        const qint64 t1 = spy.at(1).at(1).toLongLong();
        QVERIFY(t1 > t0);
    }

    void reportsFrameSize() {
        FakeCaptureSource src;
        src.setFrameSize({1920, 1080});
        QCOMPARE(src.frameSize(), QSize(1920, 1080));
    }

    void stopEmitsRequested() {
        FakeCaptureSource src;
        QVERIFY(src.start());
        QSignalSpy spy(&src, &ICaptureSource::stopped);
        src.stop();
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).value<StopReason>(), StopReason::Requested);
    }

    void injectDetachEmitsDetached() {
        FakeCaptureSource src;
        QVERIFY(src.start());
        QSignalSpy spy(&src, &ICaptureSource::stopped);
        src.injectDetach();
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).value<StopReason>(), StopReason::Detached);
    }

    void deliverFramesDisabledEmitsNothing() {
        FakeCaptureSource src;
        src.setDeliverFrames(false);
        QVERIFY(src.start());
        QSignalSpy spy(&src, &ICaptureSource::frameReady);
        src.emitOneFrame();
        QCOMPARE(spy.count(), 0);
    }

    void frameDeepCopyOwnsItsPixels() {
        FakeCaptureSource src;
        src.setFrameSize({64, 48});
        src.setScribbleAfterEmit(true);   // buffer is overwritten after emit
        QVERIFY(src.start());

        Frame captured;
        connect(&src, &ICaptureSource::frameReady, this,
                [&](const QVideoFrame& f, qint64 ts) {
                    captured = Frame::deepCopy(f, ts);
                });
        const QColor expected = src.nextFillColor();
        src.emitOneFrame();

        QVERIFY(captured.isValid());
        QCOMPARE(captured.image().size(), QSize(64, 48));
        // If deepCopy shared the source buffer, scribbling would show here.
        QCOMPARE(captured.image().pixelColor(32, 24).rgb(), expected.rgb());
    }

    void deepCopyOfInvalidFrameIsInvalid() {
        QVERIFY(!Frame::deepCopy(QVideoFrame(), 0).isValid());
    }
};

QTEST_MAIN(TestFakeCaptureSource)
#include "test_fake_capture_source.moc"
