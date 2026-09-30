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

    // Proves that a Frame produced by Frame::deepCopy survives the source
    // recycling (scribbling over) its buffer after emission. This does NOT
    // prove that Frame::deepCopy's .copy() call specifically is what makes
    // that true -- on current Qt, QVideoFrame::toImage() already returns
    // freshly-allocated storage, so this test would still pass even with
    // .copy() removed. The .copy() call is kept as belt-and-braces defensive
    // code (see the comment in Frame.cpp); this test verifies the outward
    // guarantee (deep copies are immune to source buffer recycling), not the
    // implementation detail of how that guarantee is achieved.
    void deepCopyOutlivesSourceBufferRecycle() {
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
        // If the copy shared the source buffer, scribbling would show here.
        QCOMPARE(captured.image().pixelColor(32, 24).rgb(), expected.rgb());
    }

    void deepCopyOfInvalidFrameIsInvalid() {
        QVERIFY(!Frame::deepCopy(QVideoFrame(), 0).isValid());
    }

    // fillYuyv is the one function a downstream test cannot independently
    // verify: if it wrote RGB bytes into a YUYV-declared buffer, Task 7's
    // YUY2 assertion would be checking garbage against garbage and passing
    // for the wrong reason. This test exercises the real YUYV packing and
    // BT.601 conversion end to end. The round trip is lossy (chroma
    // subsampling plus integer rounding), so a small per-channel tolerance
    // is used instead of exact equality -- a correct round trip of pure red
    // lands near (255, 0, 2), not exactly (255, 0, 0).
    void yuyvFrameRoundTripsColourApproximately() {
        FakeCaptureSource src;
        src.setFrameSize({64, 48});
        src.setPixelFormat(QVideoFrameFormat::Format_YUYV);
        QVERIFY(src.start());

        Frame captured;
        connect(&src, &ICaptureSource::frameReady, this,
                [&](const QVideoFrame& f, qint64 ts) {
                    captured = Frame::deepCopy(f, ts);
                });
        const QColor expected = src.nextFillColor();
        src.emitOneFrame();

        QVERIFY(captured.isValid());
        const QColor actual = captured.image().pixelColor(32, 24);
        const int tolerance = 4;
        QVERIFY(qAbs(actual.red() - expected.red()) <= tolerance);
        QVERIFY(qAbs(actual.green() - expected.green()) <= tolerance);
        QVERIFY(qAbs(actual.blue() - expected.blue()) <= tolerance);
    }
};

QTEST_MAIN(TestFakeCaptureSource)
#include "test_fake_capture_source.moc"
