#include <QtTest>
#include <QSignalSpy>
#include "capture/QtCaptureSource.h"
#include "device/DeviceRegistry.h"

class TestQtCaptureSource : public QObject {
    Q_OBJECT
    static bool haveCamera() { return !DeviceRegistry().available().isEmpty(); }

private slots:
    void registryEnumeratesWithoutCrashing() {
        DeviceRegistry registry;
        const auto devices = registry.available();
        for (const auto& d : devices) {
            QVERIFY(!d.id.isEmpty());
            QVERIFY(!d.description.isEmpty());
        }
    }

    void advertisedFormatsAreOrderedBestFirst() {
        if (!haveCamera()) QSKIP("no camera attached");
        QtCaptureSource src(DeviceRegistry().available().first().device);
        const auto formats = src.advertisedFormats();
        QVERIFY(!formats.isEmpty());
        QCOMPARE(formats, FormatPreference::order(formats));
    }

    void deliversAFrameFromRealHardware() {
        if (!haveCamera()) QSKIP("no camera attached");
        QtCaptureSource src(DeviceRegistry().available().first().device);
        QSignalSpy frames(&src, &ICaptureSource::frameReady);
        QVERIFY(src.start());
        QVERIFY2(frames.wait(5000), "no frame within 5s -- see spec 10.2");
        QVERIFY(!src.frameSize().isEmpty());
        src.stop();
    }

    void stopEmitsRequested() {
        if (!haveCamera()) QSKIP("no camera attached");
        QtCaptureSource src(DeviceRegistry().available().first().device);
        QVERIFY(src.start());
        QSignalSpy stopped(&src, &ICaptureSource::stopped);
        src.stop();
        QTRY_COMPARE_WITH_TIMEOUT(stopped.count(), 1, 3000);
        QCOMPARE(stopped.at(0).at(0).value<StopReason>(), StopReason::Requested);
    }

    void selectNextFormatWalksTheFallbackChain() {
        if (!haveCamera()) QSKIP("no camera attached");
        QtCaptureSource src(DeviceRegistry().available().first().device);
        const int count = src.advertisedFormats().size();
        int advanced = 0;
        while (src.selectNextFormat()) ++advanced;
        QCOMPARE(advanced, count - 1);   // exhausts, then reports false
    }
};

QTEST_MAIN(TestQtCaptureSource)
#include "test_qt_capture_source.moc"
