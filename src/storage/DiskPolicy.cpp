#include "storage/DiskPolicy.h"
#include <QStorageInfo>

bool DiskPolicy::canStartRecording(qint64 freeBytes) {
    return freeBytes >= RecordStartMinBytes;
}
bool DiskPolicy::mustStopRecording(qint64 freeBytes) {
    return freeBytes < RecordStopBytes;
}
bool DiskPolicy::canSnapshot(qint64 freeBytes) {
    return freeBytes >= SnapshotMinBytes;
}
qint64 DiskPolicy::freeBytesFor(const QString& path) {
    const QStorageInfo info(path);
    return info.isValid() ? info.bytesAvailable() : -1;
}
