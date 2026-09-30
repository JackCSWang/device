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
};

QTEST_MAIN(TestDiskPolicy)
#include "test_disk_policy.moc"
