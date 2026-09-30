#include "capture/CaptureSession.h"
#include "capture/CaptureController.h"
#include "capture/StatusModel.h"
#include "device/CameraAccessPolicy.h"
#include "device/DeviceSelection.h"
#include "output/IRecorder.h"
#include "output/SnapshotWriter.h"
#include "storage/OutputLocation.h"
#include <QFileInfo>
#include <QVideoSink>

CaptureSession::CaptureSession(IDeviceRegistry* registry, SourceFactory sourceFactory,
                               RecorderFactory recorderFactory, QString outputDir,
                               QObject* parent)
    : QObject(parent), m_registry(registry), m_sourceFactory(std::move(sourceFactory)),
      m_recorderFactory(std::move(recorderFactory)), m_outputDir(std::move(outputDir)),
      m_status(new StatusModel(this)), m_writer(new SnapshotWriter(this)) {

    // Connected here, once, for this object's whole life -- NOT inside
    // openDevice() where the controller's own connections go.
    //
    // SnapshotWriter::write() queues the JPEG encode on the global thread
    // pool and returns immediately, so a snapshot armed just before a
    // detach lands *after* the controller that armed it has been destroyed.
    // The controller is the only thing that used to listen, so that JPEG
    // saved successfully and nobody was ever told. The writer outlives every
    // pipeline; so must the thing that reports for it.
    connect(m_writer, &SnapshotWriter::written, this, [this](const QString& path, QSize) {
        m_status->setLastOutcome(tr("Last saved: %1").arg(QFileInfo(path).fileName()));
    });
    connect(m_writer, &SnapshotWriter::failed, this, [this](const QString& path, const QString&) {
        m_status->setLastOutcome(path.isEmpty()
            ? tr("The last snapshot was not saved.")
            : tr("Not saved: %1").arg(QFileInfo(path).fileName()));
    });

    connect(m_registry, &IDeviceRegistry::attached, this, &CaptureSession::onAttached);
    connect(m_registry, &IDeviceRegistry::detached, this, &CaptureSession::onDetached);
}

CaptureSession::~CaptureSession() = default;

void CaptureSession::start() {
    OutputLocation::ensureExists(m_outputDir);
    // Snapshotted before this app has asked for anything, so a denial it
    // might itself provoke cannot have happened yet.
    m_sawDeviceAtStartup = !m_registry->available().isEmpty();

    // watch()'s initial synchronous burst of attached() for every
    // already-present device, and the explicit call below (needed for the
    // "no scope at all" case, which never sees an attached() signal), can
    // both reach openPreferredDevice() for the same device -- exactly one
    // of them does the work, because it early-returns while a pipeline is
    // already open.
    m_registry->watch();
    openPreferredDevice();
}

bool CaptureSession::isRecording() const {
    return m_controller && m_controller->isRecording();
}

QVideoSink* CaptureSession::displaySink() const {
    return m_controller ? m_controller->displaySink() : nullptr;
}

QStringList CaptureSession::deviceDescriptions() const {
    QStringList out;
    out.reserve(m_devices.size());
    for (const ScopeDevice& d : m_devices) out.append(d.description);
    return out;
}

bool CaptureSession::deviceStillEnumerated(const QString& id) const {
    if (id.isEmpty()) return false;
    for (const ScopeDevice& d : m_registry->available())
        if (d.id == id) return true;
    return false;
}

void CaptureSession::setCameraAccessDenied(bool denied) {
    if (m_cameraAccessDenied == denied) return;
    m_cameraAccessDenied = denied;
    emit pipelineChanged();
}

void CaptureSession::setNeedsChoice(bool needs) {
    if (m_needsChoice == needs) return;
    m_needsChoice = needs;
    emit pipelineChanged();
}

void CaptureSession::onAttached(const ScopeDevice& device) {
    // A fresh attach edge for a device that previously errored *is* a
    // device-list change, which is one of the two things allowed to lift
    // the no-auto-reopen block (the other is retry()).
    if (device.id == m_blockedDeviceId) {
        m_blockedDeviceId.clear();
        m_consecutiveReopens = 0;
    }
    openPreferredDevice();
}

void CaptureSession::onDetached(const QString& id) {
    if (id == m_blockedDeviceId) m_blockedDeviceId.clear();
    m_devices = m_registry->available();
    emit deviceListChanged();
    emit pipelineChanged();
    // If the detached device was the open one, its own stopped() drives
    // teardown and the reopen decision. This call is for the other case:
    // nothing is open, and the device line still says something stale.
    if (!m_source) openPreferredDevice();
}

void CaptureSession::openPreferredDevice() {
    if (m_source) return;   // a pipeline is already open; nothing to do

    m_devices = m_registry->available();
    emit deviceListChanged();

    if (m_devices.isEmpty()) {
        setNeedsChoice(false);
        setCameraAccessDenied(
            CameraAccessPolicy::classify(m_sawDeviceAtStartup, 0, m_everOpenedADevice)
            == DeviceAbsenceReason::LikelyAccessDenied);
        m_status->setDeviceStatus(m_cameraAccessDenied
            ? tr("The scope is connected, but this app is not allowed to use the "
                 "camera. Open camera privacy settings to allow it.")
            : tr("No scope detected. Connect the microscope by USB."));
        return;
    }

    // Critical 1, spec 8.5 step 1: "auto-open if exactly one scope,
    // otherwise prompt". Binding devices.first() picked the laptop's
    // integrated webcam on the spec's own field hardware, which made every
    // other guarantee vacuous -- unplugging the scope mid-recording did
    // nothing, because the scope was never the source.
    const DeviceChoice choice = DeviceSelection::choose(m_devices, m_rememberedId);
    if (choice.kind != DeviceChoice::Kind::Open) {
        setNeedsChoice(choice.kind == DeviceChoice::Kind::Prompt);
        setCameraAccessDenied(false);
        m_status->setDeviceStatus(tr("Select a scope."));
        return;
    }

    // Critical 3: a device that stays enumerated while refusing to activate
    // must not be reopened on the next event-loop turn, forever.
    if (choice.deviceId == m_blockedDeviceId) return;

    for (const ScopeDevice& device : m_devices) {
        if (device.id != choice.deviceId) continue;
        setNeedsChoice(false);
        openDevice(device);
        return;
    }
}

void CaptureSession::openDevice(const ScopeDevice& device) {
    // A device is about to be opened, so any earlier denial no longer
    // applies. Without this, granting access mid-session leaves the "Open
    // camera settings" button stuck visible over a working pipeline.
    setCameraAccessDenied(false);

    m_source = m_sourceFactory(device);
    if (!m_source) {
        m_status->setDeviceStatus(tr("Could not open the scope. Nothing was saved. "
                                     "Check the cable, then reconnect the device."));
        return;
    }
    m_recorder = m_recorderFactory();
    m_controller = std::make_unique<CaptureController>(
        m_source.get(), m_recorder.get(), m_writer, m_outputDir);

    m_openDeviceId = device.id;
    m_everOpenedADevice = true;
    m_sawFrame = false;
    if (m_rememberedId != device.id) {
        m_rememberedId = device.id;
        emit rememberedDeviceIdChanged(m_rememberedId);
    }

    connect(m_controller.get(), &CaptureController::status, this,
            [this](const QString& text) { m_status->setDeviceStatus(text); });
    // The sticky half of the precedence rule (StatusModel): a recording
    // that saved names its file on a line no device-state message can
    // overwrite. This signal already existed and was consumed by nothing
    // outside tests, which is precisely why Critical 2 was invisible.
    connect(m_controller.get(), &CaptureController::recordingSaved, this,
            [this](const QString& path) {
                m_status->setLastOutcome(tr("Last saved: %1").arg(QFileInfo(path).fileName()));
            });
    connect(m_controller.get(), &CaptureController::sourceLost, this,
            [this](const QString&, StopReason reason) { onSourceLost(reason); });

    connect(m_recorder.get(), &IRecorder::finished, this, &CaptureSession::recordingChanged);
    connect(m_recorder.get(), &IRecorder::failed, this, &CaptureSession::recordingChanged);
    connect(m_recorder.get(), &IRecorder::failed, this,
            [this](const QString& path, const QString&) {
                m_status->setLastOutcome(path.isEmpty()
                    ? tr("The last recording was not saved.")
                    : tr("Not saved: %1").arg(QFileInfo(path).fileName()));
            });

    connect(m_source.get(), &ICaptureSource::frameReady, this,
            [this](const QVideoFrame& frame, qint64) { onFrame(frame); });

    m_controller->begin();
    // begin() can fail synchronously (and a source is free to emit
    // stopped() from inside start()), in which case it has already put the
    // real reason on the device line and the pipeline may already be gone.
    // Do not paper over it with "Connected to ...".
    if (m_controller)
        m_status->setDeviceStatus(tr("Connected to %1.").arg(device.description));
    emit pipelineChanged();
}

void CaptureSession::onFrame(const QVideoFrame& frame) {
    if (!m_sawFrame) {
        m_sawFrame = true;
        m_consecutiveReopens = 0;   // this device demonstrably works
        emit pipelineChanged();
    }
    if (m_frameSize != frame.size()) {
        m_frameSize = frame.size();
        emit frameSizeChanged(m_frameSize);
    }
}

void CaptureSession::onSourceLost(StopReason reason) {
    m_lastStopReason = reason;
    if (m_teardownQueued) return;
    m_teardownQueued = true;
    // Deferred deliberately -- see m_lastStopReason's comment in the header
    // for what is still unwinding beneath this signal.
    QMetaObject::invokeMethod(this, [this] { teardownPipeline(); }, Qt::QueuedConnection);
}

void CaptureSession::teardownPipeline() {
    m_teardownQueued = false;
    if (!m_controller && !m_source && !m_recorder) return;   // already torn down

    const QString lostId = m_openDeviceId;
    const StopReason reason = m_lastStopReason;
    const bool deliveredAFrame = m_sawFrame;

    // Oldest-dependent first: the controller holds raw pointers into the
    // source and recorder (and is connected to both), so it must let go of
    // them before either is released. release() + deleteLater(), not
    // reset(), so nothing is actually freed until the event loop is idle
    // again. Disconnecting the controller first means a second sourceLost
    // from it in that window cannot queue a second teardown against a
    // sender this object no longer considers its current controller.
    if (auto* controller = m_controller.release()) {
        controller->disconnect(this);
        controller->deleteLater();
    }
    if (auto* recorder = m_recorder.release()) {
        recorder->disconnect(this);
        recorder->deleteLater();
    }
    if (auto* source = m_source.release()) {
        source->disconnect(this);
        source->deleteLater();
    }
    m_openDeviceId.clear();
    m_sawFrame = false;

    emit pipelineChanged();
    emit recordingChanged();

    if (reason == StopReason::Error) {
        // Critical 3. The device is (or was) still enumerated: busy is not
        // absent. Reopening it here is what produced one full
        // open->error->teardown cycle per event-loop turn, forever, with
        // the banner alternating between the driver's cause and "Connected
        // to HD camera." So: surface it and stop. Recovery is retry() or a
        // device-list change, both of which clear m_blockedDeviceId.
        m_blockedDeviceId = lostId;
        // Important 3: on Windows a privacy-blocked camera still
        // enumerates and fails at activation, so the empty-list heuristic
        // in CameraAccessPolicy::classify() can never see it. Offer the
        // deep link *alongside* the driver's cause, which is already on the
        // device line and which this deliberately does not overwrite.
        setCameraAccessDenied(CameraAccessPolicy::activationFailureMayBeAccessDenied(
            deviceStillEnumerated(lostId), deliveredAFrame));
        return;
    }

    // Detached. Recovery depends on a future attached() edge, but
    // DeviceRegistry::refresh() only emits attached() for a device not
    // already in its known set. A detach-while-recording delays sourceLost
    // (and so this call) until the real encoder finalizes, seconds later; a
    // technician who reseats the cable inside that window produces an
    // attached() that openPreferredDevice()'s guard swallowed, because the
    // source was still (briefly) non-null. This one-shot re-poll is the
    // only thing that recovers that case.
    if (!deliveredAFrame) ++m_consecutiveReopens;
    if (m_consecutiveReopens >= MaxConsecutiveReopens) {
        m_status->setDeviceStatus(tr("The scope keeps dropping out without sending any "
                                     "video. Reconnect it, then press Retry."));
        return;
    }
    openPreferredDevice();
}

void CaptureSession::takeSnapshot() {
    if (m_controller) m_controller->takeSnapshot();
}

void CaptureSession::toggleRecording() {
    if (!m_controller) return;
    if (m_controller->isRecording()) m_controller->stopRecording();
    else m_controller->startRecording();
    emit recordingChanged();
}

void CaptureSession::selectDevice(int index) {
    if (index < 0 || index >= m_devices.size()) return;
    const ScopeDevice device = m_devices.at(index);
    m_blockedDeviceId.clear();
    m_consecutiveReopens = 0;
    if (m_rememberedId != device.id) {
        m_rememberedId = device.id;
        emit rememberedDeviceIdChanged(m_rememberedId);
    }
    if (m_source) return;   // one is already open; the choice takes effect next time
    setNeedsChoice(false);
    openDevice(device);
}

void CaptureSession::retry() {
    m_blockedDeviceId.clear();
    m_consecutiveReopens = 0;
    openPreferredDevice();
}
