#pragma once
#include "core/ICaptureSource.h"
#include "device/IDeviceRegistry.h"
#include <QObject>
#include <QSize>
#include <QString>
#include <QStringList>
#include <functional>
#include <memory>

class CaptureController;
class IRecorder;
class SnapshotWriter;
class StatusModel;
class QVideoSink;

// Owns the (source, recorder, controller) lifecycle: construction, teardown
// ordering, the reopen decision, idempotency, and the recovery policy.
//
// Why this class exists at all. All of this used to live in AppContext, in
// the app target -- which by this project's own structural rule ("all logic
// lives in microscope_core because logic in the app target is untestable")
// no test can reach. Three Criticals survived fourteen reviews in there:
//
//  C1  the device was chosen as `devices.first()`, which on the spec's own
//      field hardware (a technician with a laptop) is the integrated
//      webcam. See DeviceSelection.
//  C2  the outcome message naming a saved recording was destroyed one
//      event-loop turn later by the reopen attempt's own status. See
//      StatusModel: capture outcomes are now a separate, sticky line.
//  C3  a device that stays enumerated while refusing to stream (busy, or a
//      bandwidth failure Task 10 classifies as Error) was reopened
//      immediately, forever, once per event-loop turn. See the Error branch
//      of teardownPipeline().
//
// Everything reachable only through a real QCamera and a real
// QMediaDevices stays out: the registry and the source are injected.
// AppContext shrinks to the Q_PROPERTY relays its own header always claimed
// it was.
class CaptureSession : public QObject {
    Q_OBJECT
public:
    // A device lost this many times in a row *without ever delivering a
    // frame* stops being reopened automatically; recovery then waits for
    // retry() or a device-list change. This is the bounded half of the
    // recovery policy: the Detached path keeps its automatic reopen (a
    // reseat during the deferred-teardown window depends on it) but cannot
    // spin on a device that attaches, fails, and detaches repeatedly.
    static constexpr int MaxConsecutiveReopens = 3;

    using SourceFactory = std::function<std::unique_ptr<ICaptureSource>(const ScopeDevice&)>;
    using RecorderFactory = std::function<std::unique_ptr<IRecorder>()>;

    CaptureSession(IDeviceRegistry* registry, SourceFactory sourceFactory,
                   RecorderFactory recorderFactory, QString outputDir,
                   QObject* parent = nullptr);
    ~CaptureSession() override;

    // Snapshots "did the OS see any camera before we asked" (for the
    // privacy-denial heuristic), starts watching, and makes the first open
    // attempt. Separate from the constructor so a test can wire its spies
    // up before anything happens.
    void start();

    StatusModel* status() const { return m_status; }
    SnapshotWriter* snapshotWriter() const { return m_writer; }

    bool hasDevice() const { return m_source != nullptr; }
    // True once this pipeline has actually delivered a frame. Recording and
    // snapshots are gated on it: a recorder started before any frame has a
    // fabricated resolution and a take that cannot contain anything.
    bool hasVideo() const { return m_sawFrame; }
    bool isRecording() const;
    bool cameraAccessDenied() const { return m_cameraAccessDenied; }
    // True when more than one video input is present and none has been
    // chosen yet (spec 8.5 step 1: "auto-open if exactly one scope,
    // otherwise prompt").
    bool needsDeviceChoice() const { return m_needsChoice; }
    QStringList deviceDescriptions() const;
    QSize frameSize() const { return m_frameSize; }
    QVideoSink* displaySink() const;

    // The device id to prefer when several are present. Persisted by the
    // caller (a QSettings write is wiring); the *policy* of preferring it
    // lives in DeviceSelection, with tests.
    QString rememberedDeviceId() const { return m_rememberedId; }
    void setRememberedDeviceId(const QString& id) { m_rememberedId = id; }

    void takeSnapshot();
    void toggleRecording();
    // Index into deviceDescriptions(). Opens that device and remembers it.
    void selectDevice(int index);
    // User-initiated recovery: forgets that a device errored and tries
    // again. The Error path deliberately does not retry on its own.
    void retry();

signals:
    void pipelineChanged();
    void recordingChanged();
    void deviceListChanged();
    void frameSizeChanged(QSize size);
    void rememberedDeviceIdChanged(QString id);

private:
    void onAttached(const ScopeDevice& device);
    void onDetached(const QString& id);
    void onFrame(const QVideoFrame& frame);
    void onSourceLost(StopReason reason);
    void openPreferredDevice();
    void openDevice(const ScopeDevice& device);
    void teardownPipeline();
    void setCameraAccessDenied(bool denied);
    void setNeedsChoice(bool needs);
    bool deviceStillEnumerated(const QString& id) const;

    IDeviceRegistry* m_registry;
    SourceFactory m_sourceFactory;
    RecorderFactory m_recorderFactory;
    QString m_outputDir;
    StatusModel* m_status;
    SnapshotWriter* m_writer;

    std::unique_ptr<IRecorder> m_recorder;
    std::unique_ptr<ICaptureSource> m_source;
    std::unique_ptr<CaptureController> m_controller;

    QList<ScopeDevice> m_devices;
    QString m_openDeviceId;
    QString m_rememberedId;

    // The device id whose activation failed with the device still
    // enumerated. Not reopened automatically -- that unbounded loop was
    // Critical 3. Cleared by retry(), or by a device-list change concerning
    // that id (a fresh attach edge, or a detach).
    QString m_blockedDeviceId;

    // Carried from the sourceLost handler to the *queued* teardown, which
    // is where the reopen decision is made. Teardown must stay off
    // sourceLost's own emitting stack: that signal can be emitted from
    // inside the recorder's finished()/failed() handler with a
    // QMediaRecorder's own recorderStateChanged emission still unwinding
    // beneath it, so destroying the recorder synchronously there frees
    // memory still in use further down the call stack.
    StopReason m_lastStopReason = StopReason::Requested;

    int m_consecutiveReopens = 0;
    bool m_sawFrame = false;
    bool m_sawDeviceAtStartup = false;
    bool m_everOpenedADevice = false;
    bool m_cameraAccessDenied = false;
    bool m_needsChoice = false;
    bool m_teardownQueued = false;
    QSize m_frameSize;
};
