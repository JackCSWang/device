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

DiskSpaceCache::DiskSpaceCache(int minIntervalMs)
    : m_probe(&DiskPolicy::freeBytesFor), m_minIntervalMs(minIntervalMs) {}

void DiskSpaceCache::setProbe(std::function<qint64(const QString&)> probe) {
    m_probe = std::move(probe);
    // A new probe invalidates whatever the old one reported.
    m_since.invalidate();
    m_lastReading = -1;
    m_lastPath.clear();
}

qint64 DiskSpaceCache::freeBytesFor(const QString& path) {
    // A different path may be a different filesystem, so never serve a
    // cached reading across one.
    const bool stale = !m_since.isValid() || m_lastPath != path
                       || m_since.elapsed() >= m_minIntervalMs;
    if (stale) {
        ++m_probeCount;
        m_lastReading = m_probe(path);
        m_lastPath = path;
        m_since.restart();
    }
    return m_lastReading;
}
