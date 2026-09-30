#pragma once
#include <QString>
#include <QtGlobal>

// Free-space thresholds from spec 10.3. Exact values, not guidance.
class DiskPolicy {
public:
    static constexpr qint64 RecordStartMinBytes = 500LL * 1024 * 1024;
    static constexpr qint64 RecordStopBytes     = 100LL * 1024 * 1024;
    static constexpr qint64 SnapshotMinBytes    =  50LL * 1024 * 1024;

    static bool canStartRecording(qint64 freeBytes);
    static bool mustStopRecording(qint64 freeBytes);
    static bool canSnapshot(qint64 freeBytes);

    static qint64 freeBytesFor(const QString& path);
};
