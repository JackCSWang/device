#pragma once
#include <QMutex>
#include <QObject>
#include <QVideoFrame>

// Important 7, and spec 8.4's backpressure rule made explicit.
//
// QVideoSink::videoFrameChanged was connected with AutoConnection. If a
// backend pushes frames from its own thread, AutoConnection silently
// becomes a *queued* connection: one event per frame, each holding a
// reference to a buffer the backend is busy recycling, with nothing
// anywhere dropping stale ones. That is precisely the unbounded queue spec
// 8.4 forbids -- "an unbounded queue becomes latency, then a crash" -- and
// no test could ever see it, because FakeCaptureSource emits on the test
// thread, where AutoConnection is direct.
//
// MEASURED, on this project's reference machine (Windows 10, Qt 6.8.3
// MinGW): both the `windows` (Media Foundation) and `ffmpeg` backends
// deliver videoFrameChanged on the sink's own thread -- the main thread --
// so on Windows no queue forms and this relay's coalescing never engages.
// macOS and Linux have never been built or run, and Qt documents no
// guarantee either way, so the contract is enforced here rather than
// assumed.
//
// The rule: at most ONE pending frame. A frame offered while one is already
// pending REPLACES it -- latest wins, the older one is dropped, and the
// queue can never exceed a single slot no matter how far behind the
// consumer falls. Same-thread offers bypass the slot entirely and are
// delivered directly, so nothing is deferred or coalesced where a queue
// could not have formed in the first place.
class FrameRelay : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;

    // Callable from ANY thread. frameReady is always emitted on the thread
    // this relay lives on, which is what lets ICaptureSource promise its
    // consumers a single, known delivery thread.
    void offer(const QVideoFrame& frame, qint64 timestampUs);

    // Frames dropped because a newer one replaced them while pending. Zero
    // whenever delivery is same-thread.
    int coalescedCount() const;
    // Frames actually emitted.
    int deliveredCount() const;

signals:
    void frameReady(const QVideoFrame& frame, qint64 timestampUs);

private:
    void drain();

    mutable QMutex m_mutex;
    QVideoFrame m_pending;
    qint64 m_pendingTimestampUs = 0;
    bool m_hasPending = false;
    // True from the moment a drain is posted until it runs. This is what
    // bounds the queue: while it is set, further offers replace the pending
    // frame instead of posting another event.
    bool m_drainQueued = false;
    int m_coalesced = 0;
    int m_delivered = 0;
};
