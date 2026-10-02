#include "output/SnapshotWriter.h"
#include "core/Frame.h"
#include <QThread>
#include <QThreadPool>

namespace { constexpr int JpegQuality = 92; }

SnapshotWriter::~SnapshotWriter() {
    // Block until every task this instance queued on the global QThreadPool
    // has finished (including its emit) before this object's memory goes
    // away -- see m_pendingTasks's declaration for why this is needed.
    //
    // No deadlock risk: the task's emit is, in every case that matters here,
    // a queued cross-thread post to this object's own thread that returns
    // immediately rather than blocking on that thread actually processing
    // it. There is nothing for this loop to wait on except the pool thread
    // itself finishing its work and dropping the counter.
    while (m_pendingTasks.loadAcquire() != 0)
        QThread::msleep(1);
}

void SnapshotWriter::write(const QVideoFrame& frame, qint64 timestampUs,
                           const QString& path,
                           const Orientation& orientation) {
    // Copy now, on this thread, while the buffer is still valid.
    const Frame owned = Frame::deepCopy(frame, timestampUs);
    if (!owned.isValid()) {
        // Deferred (queued) rather than emitted inline: every other signal
        // this class emits happens asynchronously relative to write()
        // returning (it is emitted from a QThreadPool task scheduled after
        // write() returns). A caller that does
        //   writer.write(...); QSignalSpy(...).wait();
        // relies on that: QSignalSpy::wait() only observes emissions that
        // happen after it starts waiting, so an inline emit here would race
        // ahead of it and the wait would time out. Queuing keeps this path
        // consistent with the others.
        //
        // This path never touches the pool, so it needs no m_pendingTasks
        // guard: QMetaObject::invokeMethod's context-object safety already
        // cancels the call if `this` is destroyed first. The trade-off is
        // an event-loop dependency the pool-thread emits below don't have:
        // this queued call only ever runs if *this object's own thread* is
        // pumping events, whereas the pool-thread emits only need the
        // *receiver's* thread to be. A SnapshotWriter parked on a thread
        // with no event loop would silently never emit `failed` for an
        // invalid frame, while still emitting normally for encode failures.
        // In short: this class expects to live on a thread that runs an
        // event loop.
        const QString reason = tr("The frame could not be read. Nothing was saved. "
                                   "Wait for the live view to appear, then try again.");
        QMetaObject::invokeMethod(
            this, [this, path, reason] { emit failed(path, reason); },
            Qt::QueuedConnection);
        return;
    }

    const QImage image = owned.image();
    m_pendingTasks.ref();
    QThreadPool::globalInstance()->start([this, image, path, orientation] {
        // Identity returns `image` untouched and shares its data, so an
        // un-rotated snapshot costs exactly what it did before orientation
        // existed. The saved size is the TRANSFORMED size -- a quarter turn
        // of this 640x480 sensor writes a 480x640 JPEG, and `written` must
        // report what is actually on disk.
        const QImage out = orientation.apply(image);
        if (out.save(path, "JPEG", JpegQuality))
            emit written(path, out.size());
        else
            emit failed(path, tr("Could not write to %1. Nothing was saved. "
                                 "Check the folder exists and has free space.").arg(path));
        m_pendingTasks.deref();
    });
}
