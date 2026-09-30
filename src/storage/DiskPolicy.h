#pragma once
#include <QElapsedTimer>
#include <QString>
#include <QtGlobal>
#include <functional>

// Free-space thresholds from spec 10.3. Exact values, not guidance.
class DiskPolicy {
public:
    static constexpr qint64 RecordStartMinBytes = 500LL * 1024 * 1024;
    static constexpr qint64 RecordStopBytes     = 100LL * 1024 * 1024;
    static constexpr qint64 SnapshotMinBytes    =  50LL * 1024 * 1024;

    static bool canStartRecording(qint64 freeBytes);
    static bool mustStopRecording(qint64 freeBytes);
    static bool canSnapshot(qint64 freeBytes);

    // Stats the filesystem. Cheap-ish, but not free, and definitely not
    // free thirty times a second on the thread that renders live video --
    // see DiskSpaceCache.
    static qint64 freeBytesFor(const QString& path);
};

// Important 8: freeBytesFor() was called from CaptureController::onFrame(),
// i.e. once per recorded frame -- roughly 30 times a second, on the display
// thread, for the entire length of a take. Spec 8.2's threading table says
// the capture path "never blocks on disk". A QStorageInfo construction
// issues real filesystem syscalls; on a network or sleeping-spindle volume
// that is exactly the stutter the rule exists to prevent.
//
// Free space cannot change by 400 MB inside a second, so a reading held for
// ~1 s is indistinguishable from a fresh one for every threshold in spec
// 10.3 -- and the stop threshold (100 MB) sits far enough below the start
// threshold (500 MB) that a second of staleness cannot cross it from a
// healthy state.
//
// Not static state: a member of whatever owns the frame path, so tests get
// their own and there is no cross-test bleed. The probe is injectable so
// the throttle itself can be asserted by counting calls, with no
// dependency on real disk behaviour.
class DiskSpaceCache {
public:
    static constexpr int DefaultIntervalMs = 1000;

    explicit DiskSpaceCache(int minIntervalMs = DefaultIntervalMs);

    // Returns free bytes for `path`, stat'ing at most once per interval and
    // returning the last reading in between.
    qint64 freeBytesFor(const QString& path);

    // Test seam: replaces the real QStorageInfo probe.
    void setProbe(std::function<qint64(const QString&)> probe);
    // How many times the probe has actually run.
    int probeCount() const { return m_probeCount; }

private:
    std::function<qint64(const QString&)> m_probe;
    QElapsedTimer m_since;
    QString m_lastPath;
    qint64 m_lastReading = -1;
    int m_minIntervalMs;
    int m_probeCount = 0;
};
