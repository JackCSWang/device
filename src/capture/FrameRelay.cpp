#include "capture/FrameRelay.h"
#include "core/Frame.h"
#include <QThread>

void FrameRelay::offer(const QVideoFrame& frame, qint64 timestampUs) {
    if (QThread::currentThread() == thread()) {
        // No queue can form on the delivery thread itself, so hand the
        // frame straight on -- still borrowed, exactly as
        // ICaptureSource::frameReady documents. Deferring here would add a
        // full event-loop turn of latency to the live view for nothing, and
        // -- deliberately -- no allocation: this is the path Windows
        // actually takes, so it must stay a direct emit.
        {
            QMutexLocker locker(&m_mutex);
            ++m_delivered;
        }
        emit frameReady(frame, timestampUs);
        return;
    }

    // Cross-thread path (spec 8.4's stale-pixel half, N3 fix): the frame is
    // about to be parked in the pending slot and delivered a full
    // event-loop turn later, on this relay's own thread. The backend is
    // free to recycle the buffer it handed us the moment this call
    // returns -- that is exactly the borrowed-buffer rule ICaptureSource.h
    // documents, and FakeCaptureSource::setScribbleAfterEmit exists to
    // prove a consumer that skips the copy sees the corruption. So the
    // frame is deep-copied here, before it is parked, using the same
    // map + toImage().copy() Frame::deepCopy already relies on for camera
    // frames (MJPEG, YUYV, NV12, ...); wrapping the resulting QImage back
    // into a QVideoFrame is what lets it still travel through frameReady's
    // existing QVideoFrame signature.
    const Frame snapshot = Frame::deepCopy(frame, timestampUs);
    const QVideoFrame owned = snapshot.isValid() ? QVideoFrame(snapshot.image()) : frame;

    bool postDrain = false;
    {
        QMutexLocker locker(&m_mutex);
        // Latest wins. The frame already waiting is dropped, deliberately
        // and countably -- the display path "drops stale frames"
        // (spec 8.4), and a recording is timestamp-driven, so a dropped
        // frame costs frame rate and never playback speed.
        if (m_hasPending) ++m_coalesced;
        m_pending = owned;
        m_pendingTimestampUs = timestampUs;
        m_hasPending = true;
        if (!m_drainQueued) {
            m_drainQueued = true;
            postDrain = true;
        }
    }
    // Posted outside the lock: the receiving thread may already be inside
    // drain() waiting on the same mutex.
    if (postDrain)
        QMetaObject::invokeMethod(this, [this] { drain(); }, Qt::QueuedConnection);
}

void FrameRelay::drain() {
    QVideoFrame frame;
    qint64 timestampUs = 0;
    {
        QMutexLocker locker(&m_mutex);
        m_drainQueued = false;
        if (!m_hasPending) return;
        frame = m_pending;
        timestampUs = m_pendingTimestampUs;
        m_pending = QVideoFrame();
        m_hasPending = false;
        ++m_delivered;
    }
    // Emitted outside the lock: consumers do real work here (encode, disk
    // reservation) and may call back in.
    emit frameReady(frame, timestampUs);
}

int FrameRelay::coalescedCount() const {
    QMutexLocker locker(&m_mutex);
    return m_coalesced;
}

int FrameRelay::deliveredCount() const {
    QMutexLocker locker(&m_mutex);
    return m_delivered;
}
