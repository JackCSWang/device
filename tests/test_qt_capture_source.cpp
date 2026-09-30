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
        const QSize requested = src.advertisedFormats().first().resolution;
        QSignalSpy frames(&src, &ICaptureSource::frameReady);
        QVERIFY(src.start());
        QVERIFY2(frames.wait(5000), "no frame within 5s -- see spec 10.2");
        QVERIFY(!src.frameSize().isEmpty());
        // Proves format negotiation actually took effect, not just that some
        // frame (possibly the camera's own default) arrived.
        QCOMPARE(src.frameSize(), requested);
        src.stop();
    }

    // Surveys every enumerated device, not just available().first(). A shared
    // USB hub can make one device silent while another works fine (spec
    // 10.2), so per-device results are reported as data rather than a hard
    // per-device failure -- only "nothing at all delivered" fails the test.
    void surveyAllDevices() {
        if (!haveCamera()) QSKIP("no camera attached");
        const auto devices = DeviceRegistry().available();
        bool anyDelivered = false;
        qInfo().noquote() << "=== Per-device hardware survey ===";
        for (const auto& d : devices) {
            QtCaptureSource src(d.device);
            QSignalSpy frames(&src, &ICaptureSource::frameReady);
            const bool started = src.start();
            const bool delivered = started && frames.wait(5000);
            if (delivered) {
                anyDelivered = true;
                const QVideoFrame frame = frames.at(0).at(0).value<QVideoFrame>();
                const auto fmt = frame.surfaceFormat();
                qInfo().noquote() << QString(
                    "device=\"%1\" id=%2 started=%3 delivered=yes size=%4x%5 rotation=%6 mirrored=%7")
                    .arg(d.description, d.id)
                    .arg(started)
                    .arg(frame.width()).arg(frame.height())
                    .arg(int(fmt.rotation()))
                    .arg(fmt.isMirrored());
            } else {
                qInfo().noquote() << QString(
                    "device=\"%1\" id=%2 started=%3 delivered=no (no frame within 5s)")
                    .arg(d.description, d.id)
                    .arg(started);
            }
            src.stop();
        }
        QVERIFY2(anyDelivered, "no device in the survey delivered a frame");
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
