#include <QtTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include "capture/CaptureController.h"
#include "capture/CaptureSession.h"
#include "capture/StatusModel.h"
#include "core/FakeCaptureSource.h"
#include "device/IDeviceRegistry.h"
#include "output/IRecorder.h"
#include "output/SnapshotWriter.h"

// A scriptable IDeviceRegistry. This is the seam the whole extraction
// exists for: the open/teardown/reopen lifecycle used to live in
// AppContext, driven by a real QMediaDevices, so none of the four
// scenarios below could be written at all -- which is why three Criticals
// survived fourteen reviews in there.
class FakeRegistry : public IDeviceRegistry {
    Q_OBJECT
public:
    QList<ScopeDevice> available() const override { return m_devices; }
    void watch() override {
        m_watching = true;
        for (const ScopeDevice& d : m_devices) emit attached(d);
    }

    void attach(const QString& id, const QString& description) {
        if (indexOf(id) >= 0) return;
        m_devices.append(ScopeDevice{id, description, QCameraDevice()});
        if (m_watching) emit attached(m_devices.last());
    }
    void detach(const QString& id) {
        const int i = indexOf(id);
        if (i < 0) return;
        m_devices.removeAt(i);
        if (m_watching) emit detached(id);
    }
    // Removes the device without announcing it, so a test can reproduce the
    // real ordering of a physical unplug: the camera errors first and the
    // OS's own videoInputsChanged arrives later.
    void removeSilently(const QString& id) {
        const int i = indexOf(id);
        if (i >= 0) m_devices.removeAt(i);
    }

private:
    int indexOf(const QString& id) const {
        for (int i = 0; i < m_devices.size(); ++i)
            if (m_devices.at(i).id == id) return i;
        return -1;
    }
    QList<ScopeDevice> m_devices;
    bool m_watching = false;
};

// An IRecorder that resolves nothing on its own, so a test controls exactly
// when a recording's outcome becomes known.
class SessionRecorder : public IRecorder {
public:
    bool start(const QString& path, const QSize&, qreal) override {
        m_path = path;
        m_recording = true;
        return true;
    }
    void feed(const QVideoFrame&, qint64) override {}
    void finalizeAndStop() override { m_recording = false; }
    bool isRecording() const override { return m_recording; }

    void resolveFinished() { emit finished(m_path, 1000000); }
    QString path() const { return m_path; }

private:
    QString m_path;
    bool m_recording = false;
};

class TestCaptureSession : public QObject {
    Q_OBJECT
    QTemporaryDir m_dir;

    // Every source the session opens, in order, so a test can drive the one
    // that is currently live. Raw pointers: the session owns them.
    struct Harness {
        FakeRegistry registry;
        QList<FakeCaptureSource*> sources;
        QList<SessionRecorder*> recorders;
        std::unique_ptr<CaptureSession> session;
        int sourcesMade = 0;

        FakeCaptureSource* source() const { return sources.isEmpty() ? nullptr : sources.last(); }
        SessionRecorder* recorder() const {
            return recorders.isEmpty() ? nullptr : recorders.last();
        }
    };

    // Builds a session whose sources are FakeCaptureSources. `failEvery`
    // sources are created with delivery switched off so they open and then
    // never stream -- the shape of a busy device or a bandwidth failure.
    std::unique_ptr<Harness> makeHarness(bool deliverFrames = true) {
        auto h = std::make_unique<Harness>();
        Harness* raw = h.get();
        h->session = std::make_unique<CaptureSession>(
            &h->registry,
            [raw, deliverFrames](const ScopeDevice&) -> std::unique_ptr<ICaptureSource> {
                auto src = std::make_unique<FakeCaptureSource>();
                src->setFrameSize({640, 480});
                src->setDeliverFrames(deliverFrames);
                raw->sources.append(src.get());
                ++raw->sourcesMade;
                return src;
            },
            [raw]() -> std::unique_ptr<IRecorder> {
                auto rec = std::make_unique<SessionRecorder>();
                raw->recorders.append(rec.get());
                return rec;
            },
            m_dir.path());
        return h;
    }

private slots:
    void initTestCase() { qRegisterMetaType<ScopeDevice>("ScopeDevice"); }

    // The plain path: one device, it opens, and the session says so.
    void aSingleDeviceIsOpenedAndReported() {
        auto h = makeHarness();
        h->registry.attach(QStringLiteral("scope-1"), QStringLiteral("HD camera"));
        h->session->start();

        QVERIFY(h->session->hasDevice());
        QCOMPARE(h->sourcesMade, 1);
        QVERIFY(h->session->status()->deviceStatus().contains(QStringLiteral("HD camera")));
    }

    // Group 1 test 1 of 4: teardown then reopen. A detach tears the
    // pipeline down (deferred, off the emitting stack) and, because the
    // device is back by the time teardown runs, opens it again.
    void teardownIsFollowedByAReopenWhenTheDeviceIsBack() {
        auto h = makeHarness();
        h->registry.attach(QStringLiteral("scope-1"), QStringLiteral("HD camera"));
        h->session->start();
        h->source()->emitOneFrame();
        QVERIFY(h->session->hasVideo());

        // The camera errors with the node already gone -- a physical
        // unplug. The registry has not caught up yet, exactly as a real one
        // would not have.
        h->registry.removeSilently(QStringLiteral("scope-1"));
        h->source()->injectDetach();

        // Reseated before the deferred teardown runs.
        h->registry.attach(QStringLiteral("scope-1"), QStringLiteral("HD camera"));

        QTRY_COMPARE_WITH_TIMEOUT(h->sourcesMade, 2, 3000);
        QVERIFY(h->session->hasDevice());
        QVERIFY(h->session->status()->deviceStatus().contains(QStringLiteral("HD camera")));
    }

    // Group 1 test 2 of 4: a reseat *inside* the deferred-teardown window.
    // This is the case DeviceRegistry cannot signal its way out of --
    // refresh() only emits attached() for a device not already in its known
    // set, so a reseat that completes while the old pipeline is still
    // (briefly) alive produces an attached() that openPreferredDevice()
    // legitimately swallows. The one-shot re-poll at the end of teardown is
    // the only thing that recovers it. docs/manual-test-matrix.md scenario
    // 4 called this "the only verification that fix will ever receive";
    // this is that verification, automated.
    void aReseatInsideTheDeferredTeardownWindowRecovers() {
        auto h = makeHarness();
        h->registry.attach(QStringLiteral("scope-1"), QStringLiteral("HD camera"));
        h->session->start();
        h->source()->emitOneFrame();

        h->registry.removeSilently(QStringLiteral("scope-1"));
        h->source()->injectDetach();   // queues teardown; nothing torn down yet

        // The reseat lands here: still inside the window, with the old
        // source alive. The attached() this emits is swallowed.
        QVERIFY(h->session->hasDevice());
        h->registry.attach(QStringLiteral("scope-1"), QStringLiteral("HD camera"));
        QCOMPARE(h->sourcesMade, 1);   // proof the attach edge was swallowed

        // Only teardown's own re-poll can rescue it from here.
        QTRY_COMPARE_WITH_TIMEOUT(h->sourcesMade, 2, 3000);
        QVERIFY(h->session->hasDevice());
    }

    // Group 1 test 3 of 4, Critical 3: a device that stays enumerated while
    // refusing to work must not be reopened forever.
    //
    // Before the fix this ran one full open -> error -> teardown -> open
    // cycle per event-loop turn, indefinitely, with the banner alternating
    // between the driver's cause and "Connected to HD camera.". Reachable
    // for a device held by another app and for the isochronous-bandwidth
    // failures Task 10 deliberately classifies as Error.
    //
    // Sensitivity: the assertion is on the *count* of sources created. Undo
    // the Error branch in CaptureSession::teardownPipeline() -- let it fall
    // through to openPreferredDevice() like the Detached path -- and this
    // grows without bound and the QTRY below fails on the >= 3 check.
    void aPersistentlyErroringDeviceIsNotReopenedInALoop() {
        auto h = makeHarness();
        h->registry.attach(QStringLiteral("scope-1"), QStringLiteral("HD camera"));
        h->session->start();
        QCOMPARE(h->sourcesMade, 1);

        // Still enumerated -- busy is not absent -- so this is an Error,
        // not a Detach.
        emit h->source()->stopped(StopReason::Error,
                                  QStringLiteral("device in use by OtherApp"));

        // Pump the event loop well past the point where a reopen loop would
        // have run dozens of cycles.
        QTest::qWait(600);
        QVERIFY(!h->session->hasDevice());
        QCOMPARE(h->sourcesMade, 1);   // exactly one attempt, ever
        QVERIFY(h->session->status()->deviceStatus().contains(QStringLiteral("OtherApp")));

        // And it is recoverable, on user action.
        h->session->retry();
        QCOMPARE(h->sourcesMade, 2);
    }

    // The other recovery trigger for a blocked device: a device-list
    // change. A fresh attach edge means something about the device changed,
    // so the block is lifted.
    void aFreshAttachEdgeLiftsTheErrorBlock() {
        auto h = makeHarness();
        h->registry.attach(QStringLiteral("scope-1"), QStringLiteral("HD camera"));
        h->session->start();
        emit h->source()->stopped(StopReason::Error, QStringLiteral("busy"));
        QTest::qWait(300);
        QCOMPARE(h->sourcesMade, 1);

        h->registry.detach(QStringLiteral("scope-1"));
        QTest::qWait(50);
        h->registry.attach(QStringLiteral("scope-1"), QStringLiteral("HD camera"));

        QTRY_COMPARE_WITH_TIMEOUT(h->sourcesMade, 2, 3000);
    }

    // Group 1 test 4 of 4, Critical 2: the outcome message must survive the
    // device-state message that follows it.
    //
    // The exact field sequence: a recording is interrupted by a detach,
    // CaptureController reports "The scope was disconnected. The recording
    // was saved as <file>. Reconnect ...", the queued teardown runs, and
    // the reopen attempt finds nothing and reports "No scope detected."
    // Before the fix that single status string was all there was, so the
    // filename -- required, not decorative, per spec 10.1 -- was destroyed
    // one event-loop turn after it appeared.
    //
    // Sensitivity: remove the recordingSaved -> setLastOutcome connection
    // in CaptureSession::openDevice() and the final two assertions fail;
    // the lastOutcome line is empty and the filename exists nowhere.
    void aCaptureOutcomeSurvivesTheFollowingDeviceStateMessage() {
        auto h = makeHarness();
        h->registry.attach(QStringLiteral("scope-1"), QStringLiteral("HD camera"));
        h->session->start();
        h->source()->emitOneFrame();

        h->session->toggleRecording();
        QVERIFY(h->session->isRecording());
        h->source()->emitOneFrame();

        // Unplugged for real: gone from the list, then the camera errors.
        h->registry.removeSilently(QStringLiteral("scope-1"));
        h->source()->injectDetach();

        // The recorder's outcome arrives -- this is the message that names
        // the file.
        const QString path = h->recorder()->path();
        QVERIFY(!path.isEmpty());
        h->recorder()->resolveFinished();

        const QString fileName = QFileInfo(path).fileName();
        QVERIFY(h->session->status()->deviceStatus().contains(fileName));
        QCOMPARE(h->session->status()->lastOutcome(),
                 QStringLiteral("Last saved: %1").arg(fileName));

        // Now let the queued teardown and the failed reopen run. This is
        // the turn that used to destroy the filename.
        QTRY_VERIFY_WITH_TIMEOUT(
            h->session->status()->deviceStatus().contains(QStringLiteral("No scope detected")),
            3000);
        QVERIFY(!h->session->status()->deviceStatus().contains(fileName));
        // ... and here it still is.
        QCOMPARE(h->session->status()->lastOutcome(),
                 QStringLiteral("Last saved: %1").arg(fileName));
    }

    // A device that attaches, fails to stream, and detaches, over and over,
    // must not be chased forever either. The Detached path keeps its
    // automatic reopen (the reseat case above depends on it), so this is
    // the bound on it.
    void repeatedDetachesWithoutVideoStopBeingChased() {
        auto h = makeHarness(/*deliverFrames=*/false);
        h->registry.attach(QStringLiteral("scope-1"), QStringLiteral("HD camera"));
        h->session->start();

        for (int i = 0; i < CaptureSession::MaxConsecutiveReopens + 3; ++i) {
            if (!h->session->hasDevice()) break;
            h->registry.removeSilently(QStringLiteral("scope-1"));
            h->source()->injectDetach();
            h->registry.attach(QStringLiteral("scope-1"), QStringLiteral("HD camera"));
            QTest::qWait(120);
        }

        QTest::qWait(300);
        QVERIFY2(h->sourcesMade <= CaptureSession::MaxConsecutiveReopens + 1,
                 qPrintable(QStringLiteral("opened %1 times").arg(h->sourcesMade)));
        QVERIFY(h->session->status()->deviceStatus().contains(QStringLiteral("Retry")));
    }

    // Important 4: a snapshot whose JPEG encode finishes *after* the
    // pipeline that armed it is gone must still be reported. The writer's
    // signals were connected only inside the CaptureController, so the file
    // saved and nobody was told. They are connected on the session, which
    // outlives every pipeline.
    //
    // Sensitivity: move those two connects back into openDevice() and this
    // fails -- the controller is destroyed before written() arrives.
    void aSnapshotLandingAfterTeardownIsStillReported() {
        auto h = makeHarness();
        h->registry.attach(QStringLiteral("scope-1"), QStringLiteral("HD camera"));
        h->session->start();
        h->source()->emitOneFrame();
        h->session->takeSnapshot();
        h->source()->emitOneFrame();      // the armed frame; encode is queued

        // Tear the whole pipeline down immediately, before the pool task
        // can possibly have finished.
        h->registry.removeSilently(QStringLiteral("scope-1"));
        h->source()->injectDetach();
        QTRY_VERIFY_WITH_TIMEOUT(!h->session->hasDevice(), 3000);

        QTRY_VERIFY_WITH_TIMEOUT(
            h->session->status()->lastOutcome().startsWith(QStringLiteral("Last saved: ")),
            10000);
        QVERIFY(h->session->status()->lastOutcome().endsWith(QStringLiteral(".jpg")));
    }

    // Important 1's other half, at the session level: an unplug must not be
    // reported as a permissions problem. The scope is plugged in before the
    // app starts (the normal case), so sawDeviceAtStartup is true; a
    // pipeline then ran, which proves access was granted.
    void anUnplugAfterAWorkingPipelineIsNotReportedAsADenial() {
        auto h = makeHarness();
        h->registry.attach(QStringLiteral("scope-1"), QStringLiteral("HD camera"));
        h->session->start();
        h->source()->emitOneFrame();

        h->registry.detach(QStringLiteral("scope-1"));
        h->source()->injectDetach();

        QTRY_VERIFY_WITH_TIMEOUT(
            h->session->status()->deviceStatus().contains(QStringLiteral("No scope detected")),
            3000);
        QVERIFY(!h->session->cameraAccessDenied());
    }

    // The device line must not be a write-once field: a transient error
    // followed by a healthy pipeline has to clear.
    void aTransientErrorDoesNotPersistOverAHealthyPipeline() {
        auto h = makeHarness();
        h->registry.attach(QStringLiteral("scope-1"), QStringLiteral("HD camera"));
        h->session->start();
        emit h->source()->stopped(StopReason::Error, QStringLiteral("busy"));
        QTest::qWait(200);
        QVERIFY(h->session->status()->deviceStatus().contains(QStringLiteral("busy")));

        h->session->retry();
        h->source()->emitOneFrame();
        QVERIFY(!h->session->status()->deviceStatus().contains(QStringLiteral("busy")));
        QVERIFY(h->session->status()->deviceStatus().contains(QStringLiteral("HD camera")));
    }
};

QTEST_MAIN(TestCaptureSession)
#include "test_capture_session.moc"
