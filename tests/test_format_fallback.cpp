#include <QtTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include "capture/CaptureController.h"
#include "core/FakeCaptureSource.h"
#include "output/QtRecorder.h"
#include "output/SnapshotWriter.h"

class TestFormatFallback : public QObject {
    Q_OBJECT
    QTemporaryDir m_dir;

private slots:
    void silentOpenWalksDownTheChain() {
        FakeCaptureSource src;
        src.setDeliverFrames(false);     // opens fine, streams nothing
        src.setFallbackCount(2);
        QtRecorder rec; SnapshotWriter writer;
        CaptureController c(&src, &rec, &writer, m_dir.path());

        QSignalSpy exhausted(&c, &CaptureController::formatsExhausted);
        c.begin();

        // 3 timeouts: initial format, then two fallbacks.
        QVERIFY(exhausted.wait(CaptureController::FirstFrameTimeoutMs * 4 + 3000));
        QCOMPARE(src.fallbacksUsed(), 2);
    }

    void exhaustionReportsWhatTheScopeAdvertised() {
        FakeCaptureSource src;
        src.setDeliverFrames(false);
        src.setFallbackCount(0);
        QtRecorder rec; SnapshotWriter writer;
        CaptureController c(&src, &rec, &writer, m_dir.path());

        QSignalSpy exhausted(&c, &CaptureController::formatsExhausted);
        QSignalSpy msgs(&c, &CaptureController::status);
        c.begin();

        QVERIFY(exhausted.wait(CaptureController::FirstFrameTimeoutMs + 3000));
        QVERIFY(msgs.count() > 0);
        // Must name an action, per spec 10.
        QVERIFY(msgs.last().at(0).toString().contains(QStringLiteral("port")));
    }

    void aFrameArrivingStopsTheFallbackWalk() {
        FakeCaptureSource src;
        src.setFallbackCount(3);
        QtRecorder rec; SnapshotWriter writer;
        CaptureController c(&src, &rec, &writer, m_dir.path());

        QSignalSpy exhausted(&c, &CaptureController::formatsExhausted);
        c.begin();
        src.emitOneFrame();
        QTest::qWait(CaptureController::FirstFrameTimeoutMs + 1000);

        QCOMPARE(exhausted.count(), 0);
        QCOMPARE(src.fallbacksUsed(), 0);
    }
};

QTEST_MAIN(TestFormatFallback)
#include "test_format_fallback.moc"
