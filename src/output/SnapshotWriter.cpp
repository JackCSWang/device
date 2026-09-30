#include "output/SnapshotWriter.h"
#include "core/Frame.h"
#include <QThreadPool>
#include <QtConcurrent>

namespace { constexpr int JpegQuality = 92; }

void SnapshotWriter::write(const QVideoFrame& frame, qint64 timestampUs,
                           const QString& path) {
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
        const QString reason = tr("The frame could not be read. Nothing was saved.");
        QMetaObject::invokeMethod(
            this, [this, path, reason] { emit failed(path, reason); },
            Qt::QueuedConnection);
        return;
    }

    const QImage image = owned.image();
    QThreadPool::globalInstance()->start([this, image, path] {
        if (image.save(path, "JPEG", JpegQuality))
            emit written(path, image.size());
        else
            emit failed(path, tr("Could not write to %1. Nothing was saved. "
                                 "Check the folder exists and has free space.").arg(path));
    });
}
