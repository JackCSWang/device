#include <QtTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include "capture/CaptureController.h"
#include "capture/QtCaptureSource.h"
#include "device/DeviceRegistry.h"
#include "output/QtRecorder.h"
#include "output/SnapshotWriter.h"

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

    // Important 7 / N2 fix. frameReady's documented threading contract is
    // "always emitted on the thread the source lives on" -- but that much is
    // *guaranteed* by FrameRelay regardless of what the backend does: a
    // cross-thread offer is queued onto the relay's own thread before
    // frameReady is ever emitted (see FrameRelay::offer()). So
    // `QCOMPARE(seen, src.thread())` alone is tautological: it restates the
    // relay's contract, not an observation of the backend, and cannot fail
    // no matter which thread Media Foundation/AVFoundation/V4L2 actually
    // delivers on.
    //
    // What actually distinguishes "the backend delivers same-thread" from
    // "the backend delivers cross-thread and the relay silently reroutes
    // it" is coalescedCount(): zero means every offer took the direct,
    // same-thread fast path in FrameRelay::offer() (the `if
    // (QThread::currentThread() == thread())` branch); any cross-thread
    // offer that queued instead would show up there once a second frame
    // arrives while the first is still pending. This records what the
    // backend actually does on this machine: on Windows 10 / Qt 6.8.3, both
    // the `windows` (Media Foundation) and `ffmpeg` backends deliver on the
    // sink's own thread, so coalescedCount() stays 0. macOS and Linux have
    // never been run.
    void framesArriveOnTheSourcesOwnThread() {
        if (!haveCamera()) QSKIP("no camera attached");
        QtCaptureSource src(DeviceRegistry().available().first().device);
        QThread* seen = nullptr;
        int count = 0;
        connect(&src, &ICaptureSource::frameReady, &src,
                [&](const QVideoFrame&, qint64) {
                    seen = QThread::currentThread();
                    ++count;
                });
        QVERIFY(src.start());
        QTRY_VERIFY_WITH_TIMEOUT(count > 0, 5000);
        qInfo().noquote() << QStringLiteral(
            "frameReady delivered on %1 (source thread %2); frames=%3; coalesced=%4")
            .arg(reinterpret_cast<quintptr>(seen))
            .arg(reinterpret_cast<quintptr>(src.thread()))
            .arg(count)
            .arg(src.relay().coalescedCount());
        QCOMPARE(seen, src.thread());
        // The actual observable: zero coalesced frames means the relay's
        // same-thread fast path was taken every time, i.e. the backend
        // really did deliver on this thread rather than the relay having
        // silently rerouted a cross-thread delivery onto it.
        QCOMPARE(src.relay().coalescedCount(), 0);
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
        // ICaptureSource::stop()'s contract is that this is synchronous --
        // CaptureController's stall handler depends on onSourceStopped
        // running re-entrantly inside the stop() call. Assert it directly
        // rather than with QTRY, which would pass for a deferred emission.
        QCOMPARE(stopped.count(), 1);
        QCOMPARE(stopped.at(0).at(0).value<StopReason>(), StopReason::Requested);
    }

    // Must-fix minor 4, and docs/manual-test-matrix.md scenario 5 -- "the
    // highest-priority untested item in the whole project". The
    // Detached-vs-Error classification decides the wording a technician
    // sees, and nothing but a human with a real scope had ever exercised
    // the Error half. "Disconnected" sends them to check a cable that is
    // fine whenever the real cause is a device held by another app.
    //
    // This provokes it for real: hold the device open on one handle so it
    // stays enumerated, then open it again from a second. The pure mapping
    // has unit tests of its own now too (test_camera_access_policy::
    // aStillEnumeratedDeviceErrorIsNotADetach); this is the integration
    // check that QtCaptureSource asks the right question of the real
    // backend, and that the wording downstream of it is right.
    //
    // QSKIPs rather than fails when the backend permits two concurrent
    // readers of one device: "device busy" then genuinely cannot be
    // provoked here, and asserting anything about it would be dishonest.
    void aBusyDeviceIsAnErrorAndIsNotCalledDisconnected() {
        if (!haveCamera()) QSKIP("no camera attached");
        const ScopeDevice device = DeviceRegistry().available().first();

        QtCaptureSource holder(device.device);
        QSignalSpy holderFrames(&holder, &ICaptureSource::frameReady);
        QVERIFY(holder.start());
        if (!holderFrames.wait(5000))
            QSKIP("the holding handle never streamed, so the device is not actually busy");

        // Still enumerated -- that is the whole point. Busy is not absent,
        // and it is the only thing that separates Error from Detached.
        bool stillListed = false;
        for (const auto& d : DeviceRegistry().available())
            if (d.id == device.id) stillListed = true;
        QVERIFY(stillListed);

        QtCaptureSource contender(device.device);
        QtRecorder rec;
        SnapshotWriter writer;
        QTemporaryDir dir;
        CaptureController c(&contender, &rec, &writer, dir.path());
        QSignalSpy lost(&c, &CaptureController::sourceLost);
        c.begin();

        const bool errored = lost.count() > 0 || lost.wait(10000);
        holder.stop();
        contender.stop();
        if (!errored)
            QSKIP("this backend permits two concurrent readers of one device, so a busy "
                  "device cannot be provoked here (see matrix scenario 5)");

        QCOMPARE(lost.at(0).at(1).value<StopReason>(), StopReason::Error);
        const QString message = lost.at(0).at(0).toString();
        qInfo().noquote() << "busy-device message:" << message;
        QVERIFY2(!message.contains(QStringLiteral("disconnected")), qPrintable(message));
        QVERIFY2(message.contains(QStringLiteral("stopped")), qPrintable(message));
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
