#pragma once
#include <QAtomicInt>
#include <QObject>
#include <QSize>
#include <QString>
#include "view/Orientation.h"
#include <QVideoFrame>

// Writes full-resolution JPEG snapshots. ZOOM state is deliberately not a
// parameter and there is no way to pass one: snapshots are physically
// incapable of being cropped (spec 7.2, 9).
//
// Orientation is different and IS applied here. A quarter turn or a mirror
// reorders pixels without discarding any, so it cannot violate the
// full-sensor-frame guarantee the way a crop would -- and the operator
// expects the saved file to match the view they were looking at (spec 9,
// revision 3). It is applied inside the pool task, not on the calling
// thread, for the same reason the JPEG encode is.
//
// The deep copy happens synchronously on the calling thread; JPEG encoding
// happens on the global thread pool, because encoding a 1080p frame takes
// tens of milliseconds -- a visible stutter on the UI thread, and dropped
// frames on the capture thread (spec 8.2).
class SnapshotWriter : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    ~SnapshotWriter() override;
    // orientation defaults to identity so the parameter can never silently
    // change an existing call site's behaviour; callers that want the
    // operator's orientation baked in must pass it explicitly.
    void write(const QVideoFrame& frame, qint64 timestampUs, const QString& path,
               const Orientation& orientation = {});

signals:
    void written(QString path, QSize size);
    void failed(QString path, QString reason);

private:
    // Guards against the global QThreadPool outliving this object: bumped
    // before a task is queued, dropped as the very last thing the task does
    // (after its emit). The destructor blocks until this reads zero, so no
    // queued task can ever run against a destroyed SnapshotWriter. This is
    // not exotic: application teardown routinely destroys the writer before
    // the global QThreadPool drains its queue.
    QAtomicInt m_pendingTasks{0};
};
