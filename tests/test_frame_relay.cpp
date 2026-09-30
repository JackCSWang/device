#include <QtTest>
#include <QSignalSpy>
#include <QThread>
#include "capture/FrameRelay.h"
#include "capture/CaptureController.h"
#include "core/FakeCaptureSource.h"
#include "output/QtRecorder.h"
#include "output/SnapshotWriter.h"
#include <QTemporaryDir>
#include <atomic>

namespace {
QVideoFrame makeFrame(int w, int h, int fill) {
    QVideoFrameFormat format({w, h}, QVideoFrameFormat::Format_RGBX8888);
    QVideoFrame frame(format);
    if (frame.map(QVideoFrame::WriteOnly)) {
        QImage view(frame.bits(0), w, h, frame.bytesPerLine(0), QImage::Format_RGBX8888);
        view.fill(QColor::fromHsv(fill % 360, 255, 255));
        frame.unmap();
    }
    return frame;
}
} // namespace

// A capture source whose frames genuinely come from another thread, so the
// seam is exercised the way a backend that pushes from its own thread would
// exercise it. FakeCaptureSource emits on the test thread, which is exactly
// why Important 7 was invisible to every existing test.
class WorkerThreadSource : public ICaptureSource {
    Q_OBJECT
public:
    // Exactly what QtCaptureSource does: the relay is the single delivery
    // point, and forwarding its signal is what makes frameReady's
    // threading contract true for consumers.
    WorkerThreadSource() {
        connect(&m_relay, &FrameRelay::frameReady, this, &ICaptureSource::frameReady);
    }

    bool start() override { return true; }
    void stop() override { emit stopped(StopReason::Requested, QString()); }
    QSize frameSize() const override { return {320, 240}; }

    // Pushes `count` frames from a freshly started worker thread and blocks
    // until that thread is done offering them.
    void pushFromWorker(int count) {
        QThread worker;
        std::atomic<int> offered{0};
        QObject context;
        context.moveToThread(&worker);
        QObject::connect(&worker, &QThread::started, &context, [&] {
            for (int i = 0; i < count; ++i) {
                m_relay.offer(makeFrame(320, 240, i * 7), i * 33333LL);
                ++offered;
            }
            worker.quit();
        });
        worker.start();
        QVERIFY(worker.wait(10000));
        QCOMPARE(offered.load(), count);
    }

    FrameRelay& relay() { return m_relay; }

private:
    FrameRelay m_relay;
};

// N3 fix's double: unlike WorkerThreadSource above, which allocates a fresh
// QVideoFrame per offer (so there is never anything to recycle), this one
// offers a single buffer, reused, and scribbles over it immediately after
// offer() returns -- the same trick FakeCaptureSource::setScribbleAfterEmit
// uses to catch a consumer that skipped the deep copy for the same-thread
// contract test. Here it exercises the cross-thread pending slot instead.
class ReusingWorkerThreadSource : public ICaptureSource {
    Q_OBJECT
public:
    ReusingWorkerThreadSource() {
        connect(&m_relay, &FrameRelay::frameReady, this, &ICaptureSource::frameReady);
    }

    bool start() override { return true; }
    void stop() override { emit stopped(StopReason::Requested, QString()); }
    QSize frameSize() const override { return {320, 240}; }

    // Fills the one buffer with `offeredHue`, offers it, then immediately
    // overwrites that SAME buffer with `scribbleHue` -- all from a worker
    // thread, so the offer takes the relay's cross-thread path and a full
    // event-loop turn separates it from delivery. Blocks until the worker
    // is done.
    void offerThenScribble(int offeredHue, int scribbleHue) {
        QThread worker;
        QObject context;
        context.moveToThread(&worker);
        QVideoFrame frame(QVideoFrameFormat({320, 240}, QVideoFrameFormat::Format_RGBX8888));
        QObject::connect(&worker, &QThread::started, &context, [&] {
            fill(frame, offeredHue);
            m_relay.offer(frame, 0);
            fill(frame, scribbleHue);   // the backend "recycles" the buffer
            worker.quit();
        });
        worker.start();
        QVERIFY(worker.wait(10000));
    }

    FrameRelay& relay() { return m_relay; }

private:
    static void fill(QVideoFrame& frame, int hue) {
        if (!frame.map(QVideoFrame::WriteOnly)) return;
        QImage view(frame.bits(0), frame.width(), frame.height(), frame.bytesPerLine(0),
                    QImage::Format_RGBX8888);
        view.fill(QColor::fromHsv(hue % 360, 255, 255));
        frame.unmap();
    }

    FrameRelay m_relay;
};

class TestFrameRelay : public QObject {
    Q_OBJECT
    QTemporaryDir m_dir;

private slots:
    // A same-thread offer must not be deferred or coalesced: no queue can
    // form there, and deferring would add an event-loop turn of latency to
    // the live view for nothing. This is the path Windows actually takes
    // (both the `windows` and `ffmpeg` backends deliver
    // videoFrameChanged on the sink's own thread), so it is also the
    // guarantee that this whole mechanism costs nothing there.
    void sameThreadOffersAreDeliveredDirectly() {
        FrameRelay relay;
        QSignalSpy frames(&relay, &FrameRelay::frameReady);

        relay.offer(makeFrame(320, 240, 1), 1000);
        // No event loop has been pumped, so a queued delivery would not be
        // here yet.
        QCOMPARE(frames.count(), 1);
        relay.offer(makeFrame(320, 240, 2), 2000);
        QCOMPARE(frames.count(), 2);
        QCOMPARE(relay.coalescedCount(), 0);
        QCOMPARE(relay.deliveredCount(), 2);
    }

    // Spec 8.4's rule, asserted: however far behind the consumer is, the
    // queue never exceeds one frame. 200 frames pushed from a worker
    // thread, with this thread's event loop not running, must leave at
    // most one pending -- so the overwhelming majority are dropped by
    // replacement, not accumulated.
    //
    // Sensitivity: replace the relay with a plain queued connection (one
    // event per frame) and coalescedCount() is 0 while ~200 frames arrive
    // -- both assertions below fail.
    void aWorkerThreadFloodIsCoalescedToOneFrame() {
        WorkerThreadSource src;
        QSignalSpy frames(&src.relay(), &FrameRelay::frameReady);

        src.pushFromWorker(200);

        // Nothing delivered yet: this thread has not returned to its event
        // loop, and cross-thread offers are queued by design.
        QCOMPARE(frames.count(), 0);
        QVERIFY2(src.relay().coalescedCount() >= 198,
                 qPrintable(QStringLiteral("only %1 of 200 were coalesced")
                                .arg(src.relay().coalescedCount())));

        // Pumping the loop delivers the single survivor -- the latest.
        QTRY_COMPARE_WITH_TIMEOUT(frames.count(), 1, 3000);
        QCOMPARE(src.relay().deliveredCount(), 1);
        QCOMPARE(src.relay().coalescedCount() + src.relay().deliveredCount(), 200);

        const QVideoFrame delivered = frames.at(0).at(0).value<QVideoFrame>();
        QCOMPARE(delivered.size(), QSize(320, 240));
        // Latest wins, so the survivor carries the last timestamp offered.
        QCOMPARE(frames.at(0).at(1).toLongLong(), 199 * 33333LL);
    }

    // N3 fix: the cross-thread pending slot must own its pixels, not just
    // bound the queue to one slot. aWorkerThreadFloodIsCoalescedToOneFrame
    // above cannot see this -- its double allocates a fresh QVideoFrame per
    // offer, so nothing is ever recycled. This double offers ONE buffer and
    // scribbles over it immediately after offer() returns, simulating a
    // backend that recycles its buffer before the relay's queued drain()
    // -- a full event-loop turn later -- has a chance to run.
    //
    // Sensitivity: store the raw offered frame in the pending slot instead
    // of a deep copy (revert the cross-thread half of FrameRelay::offer())
    // and this fails -- the delivered pixel is the scribble colour, not the
    // one that was offered.
    void aReusedBufferScribbledAfterOfferIsNotDeliveredStale() {
        ReusingWorkerThreadSource src;
        QSignalSpy frames(&src.relay(), &FrameRelay::frameReady);

        const int offeredHue = 40;
        const int scribbleHue = 220;
        src.offerThenScribble(offeredHue, scribbleHue);

        QTRY_COMPARE_WITH_TIMEOUT(frames.count(), 1, 3000);
        const QVideoFrame delivered = frames.at(0).at(0).value<QVideoFrame>();
        const QImage image = delivered.toImage();
        QVERIFY(!image.isNull());
        const QColor pixel = image.pixelColor(image.width() / 2, image.height() / 2);
        QCOMPARE(pixel.hue(), offeredHue);
    }

    // Delivery must land on the relay's own thread, not the worker's --
    // that is the promise ICaptureSource::frameReady makes to every
    // consumer, and the reason a consumer may use AutoConnection safely.
    void deliveryHappensOnTheRelaysOwnThread() {
        WorkerThreadSource src;
        QThread* seen = nullptr;
        connect(&src.relay(), &FrameRelay::frameReady, &src,
                [&](const QVideoFrame&, qint64) { seen = QThread::currentThread(); });

        src.pushFromWorker(5);
        QTRY_VERIFY_WITH_TIMEOUT(seen != nullptr, 3000);
        QCOMPARE(seen, QThread::currentThread());
        QCOMPARE(seen, src.relay().thread());
    }

    // The whole pipeline, driven across a thread boundary through the
    // seam: CaptureController must see the frames, cancel its first-frame
    // watchdog, and be able to save a snapshot from one.
    void theControllerWorksWhenFramesCrossAThreadBoundary() {
        WorkerThreadSource src;
        QtRecorder rec;
        SnapshotWriter writer;
        CaptureController c(&src, &rec, &writer, m_dir.path());

        QSignalSpy timedOut(&c, &CaptureController::firstFrameTimedOut);
        QSignalSpy saved(&c, &CaptureController::snapshotSaved);
        QSignalSpy msgs(&c, &CaptureController::status);
        QSignalSpy relayed(&src.relay(), &FrameRelay::frameReady);

        c.begin();
        src.pushFromWorker(50);
        // Wait for the coalesced survivor to actually reach the controller
        // rather than assuming a fixed delay does it.
        QTRY_VERIFY_WITH_TIMEOUT(relayed.count() >= 1, 5000);

        c.takeSnapshot();
        src.pushFromWorker(10);    // the armed frame
        QTRY_VERIFY_WITH_TIMEOUT(relayed.count() >= 2, 5000);

        QTRY_VERIFY_WITH_TIMEOUT(saved.count() >= 1, 15000);
        QVERIFY(QFileInfo(saved.at(0).at(0).toString()).size() > 0);
        QCOMPARE(timedOut.count(), 0);
        for (const auto& call : msgs)
            QVERIFY2(!call.at(0).toString().contains(QStringLiteral("No video yet")),
                     qPrintable(call.at(0).toString()));
    }
};

QTEST_MAIN(TestFrameRelay)
#include "test_frame_relay.moc"
