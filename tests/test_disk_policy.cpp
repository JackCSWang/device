#include <QtTest>
#include <QDir>
#include "storage/DiskPolicy.h"

class TestDiskPolicy : public QObject {
    Q_OBJECT
    static constexpr qint64 MB = 1024LL * 1024LL;
private slots:
    void recordingNeedsFiveHundredMegabytes() {
        QVERIFY(DiskPolicy::canStartRecording(500 * MB));
        QVERIFY(DiskPolicy::canStartRecording(501 * MB));
        QVERIFY(!DiskPolicy::canStartRecording(499 * MB));
    }
    void recordingStopsAtOneHundredMegabytes() {
        QVERIFY(DiskPolicy::mustStopRecording(99 * MB));
        QVERIFY(DiskPolicy::mustStopRecording(0));
        QVERIFY(!DiskPolicy::mustStopRecording(100 * MB));
    }
    void snapshotNeedsFiftyMegabytes() {
        QVERIFY(DiskPolicy::canSnapshot(50 * MB));
        QVERIFY(!DiskPolicy::canSnapshot(49 * MB));
    }
    // A running recording must not be killed the instant it starts.
    void stopThresholdIsBelowStartThreshold() {
        QVERIFY(DiskPolicy::RecordStopBytes < DiskPolicy::RecordStartMinBytes);
    }
    void negativeFreeSpaceIsTreatedAsFull() {
        QVERIFY(!DiskPolicy::canStartRecording(-1));
        QVERIFY(!DiskPolicy::canSnapshot(-1));
        QVERIFY(DiskPolicy::mustStopRecording(-1));
    }
    void queriesRealFilesystem() {
        QVERIFY(DiskPolicy::freeBytesFor(QDir::tempPath()) > 0);
    }
    // Important 8: freeBytesFor() was called from CaptureController::
    // onFrame(), once per recorded frame -- ~30/s on the display thread,
    // for the length of a take -- against spec 8.2's "never blocks on
    // disk". The throttle is asserted by counting actual probes, not by
    // timing anything, so it cannot flake.
    //
    // Sensitivity: remove the interval check from
    // DiskSpaceCache::freeBytesFor() and probeCount() becomes 100.
    void repeatedQueriesWithinTheIntervalStatOnce() {
        DiskSpaceCache cache(1000);
        int calls = 0;
        cache.setProbe([&calls](const QString&) { ++calls; return 700 * MB; });

        for (int i = 0; i < 100; ++i)
            QCOMPARE(cache.freeBytesFor(QStringLiteral("/out")), 700 * MB);

        QCOMPARE(calls, 1);
        QCOMPARE(cache.probeCount(), 1);
    }

    // ... and the reading is genuinely refreshed once the interval passes,
    // or a disk filling up during a long take would never be noticed.
    void theReadingIsRefreshedAfterTheInterval() {
        DiskSpaceCache cache(20);
        qint64 answer = 700 * MB;
        cache.setProbe([&answer](const QString&) { return answer; });

        QCOMPARE(cache.freeBytesFor(QStringLiteral("/out")), 700 * MB);
        answer = 10 * MB;
        QCOMPARE(cache.freeBytesFor(QStringLiteral("/out")), 700 * MB);   // still cached
        QTest::qWait(60);
        QCOMPARE(cache.freeBytesFor(QStringLiteral("/out")), 10 * MB);
        QCOMPARE(cache.probeCount(), 2);
    }

    // A different path may be a different filesystem, so a cached reading
    // must never be served across one.
    void aDifferentPathIsNeverServedFromCache() {
        DiskSpaceCache cache(100000);
        cache.setProbe([](const QString& p) {
            return p == QStringLiteral("/a") ? 700 * MB : 10 * MB;
        });
        QCOMPARE(cache.freeBytesFor(QStringLiteral("/a")), 700 * MB);
        QCOMPARE(cache.freeBytesFor(QStringLiteral("/b")), 10 * MB);
        QCOMPARE(cache.probeCount(), 2);
    }

    // The default probe is the real filesystem, so a cache constructed
    // without setProbe() behaves like DiskPolicy::freeBytesFor().
    void theDefaultProbeIsTheRealFilesystem() {
        DiskSpaceCache cache;
        QVERIFY(cache.freeBytesFor(QDir::tempPath()) > 0);
        QCOMPARE(cache.probeCount(), 1);
    }
};

QTEST_MAIN(TestDiskPolicy)
#include "test_disk_policy.moc"
