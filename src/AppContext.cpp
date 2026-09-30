#include "AppContext.h"
#include "capture/CaptureController.h"
#include "capture/QtCaptureSource.h"
#include "device/DeviceRegistry.h"
#include "output/QtRecorder.h"
#include "output/SnapshotWriter.h"
#include "storage/OutputLocation.h"
#include "view/ViewTransformModel.h"
#include <QDesktopServices>
#include <QUrl>
#include <QVideoSink>

AppContext::AppContext(QObject* parent)
    : QObject(parent),
      m_registry(new DeviceRegistry(this)),
      m_transform(new ViewTransformModel(this)),
      m_writer(new SnapshotWriter(this)),
      m_outputDir(OutputLocation::defaultDirectory()) {

    OutputLocation::ensureExists(m_outputDir);

    // openFirstAvailableDevice() is idempotent (see below): watch()'s
    // initial synchronous burst of attached() for every already-present
    // device, and this constructor's own call below (needed for the "no
    // scope at all" case, which never sees an attached() signal), can both
    // reach here for the same device -- exactly one of them does the work.
    connect(m_registry, &DeviceRegistry::attached, this,
            [this](const ScopeDevice&) { openFirstAvailableDevice(); });
    connect(m_registry, &DeviceRegistry::detached, this, [this](const QString&) {
        emit pipelineChanged();
    });

    m_registry->watch();
    openFirstAvailableDevice();
}

AppContext::~AppContext() = default;

void AppContext::openFirstAvailableDevice() {
    if (m_source) return;   // a pipeline is already open; nothing to do

    const auto devices = m_registry->available();
    if (devices.isEmpty()) {
        setStatus(tr("No scope detected. Connect the microscope by USB."));
        return;
    }

    auto* source = new QtCaptureSource(devices.first().device);
    m_source.reset(source);
    m_recorder = std::make_unique<QtRecorder>();
    m_controller = std::make_unique<CaptureController>(
        m_source.get(), m_recorder.get(), m_writer, m_outputDir);

    connect(m_controller.get(), &CaptureController::status,
            this, [this](const QString& s) { setStatus(s); });
    connect(m_controller.get(), &CaptureController::sourceLost, this, [this] {
        // Defer the actual teardown off this signal's own emitting stack.
        // sourceLost can be emitted from inside CaptureController's own
        // recorder-finished()/failed() handler, with a QMediaRecorder's
        // recorderStateChanged emission (and its backend's frames) still
        // unwinding beneath it. Deleting that recorder -- and the
        // controller and source -- synchronously here would free memory
        // still in use further down the call stack. Queueing
        // teardownPipeline() lets everything currently emitting finish
        // unwinding back to the event loop first.
        QMetaObject::invokeMethod(this, [this] { teardownPipeline(); }, Qt::QueuedConnection);
    });
    connect(m_recorder.get(), &IRecorder::finished,
            this, &AppContext::recordingChanged);
    // A failed recording also makes isRecording() false (IRecorder's
    // contract), but nothing previously told QML that: without this,
    // `recording` stays cached true and the button latches on "Stop
    // recording" after a failed take. Signal-to-signal, exactly like
    // finished() above -- not a lambda, which could reach recording control
    // and reopen the start()-from-a-handler hazard.
    connect(m_recorder.get(), &IRecorder::failed,
            this, &AppContext::recordingChanged);

    connect(m_source.get(), &ICaptureSource::frameReady, this,
            [this](const QVideoFrame& f, qint64) {
                m_transform->setFrameSize(f.size());
            });

    m_controller->begin();
    setStatus(tr("Connected to %1.").arg(devices.first().description));
    emit pipelineChanged();
}

void AppContext::teardownPipeline() {
    if (!m_controller && !m_source && !m_recorder) return;   // already torn down

    // Oldest-dependent first: the controller holds raw pointers into the
    // source and recorder (and is connected to both), so it must let go of
    // them before either is released. release() + deleteLater(), not
    // reset(), so nothing is actually freed until the event loop is idle
    // again -- this always runs from the queued call in the sourceLost
    // handler above, but stays correct even if that ever changes. By the
    // time sourceLost fires, CaptureController has already finalized any
    // recording that was in flight (see its own ordering guarantees), so
    // there is nothing left here to finalize.
    if (auto* controller = m_controller.release()) controller->deleteLater();
    if (auto* recorder = m_recorder.release()) recorder->deleteLater();
    if (auto* source = m_source.release()) source->deleteLater();

    emit pipelineChanged();
    emit recordingChanged();
}

QVideoSink* AppContext::videoSink() const {
    return m_controller ? m_controller->displaySink() : nullptr;
}

bool AppContext::recording() const {
    return m_controller && m_controller->isRecording();
}

void AppContext::snapshot() {
    if (m_controller) m_controller->takeSnapshot();
}

void AppContext::toggleRecording() {
    if (!m_controller) return;
    if (m_controller->isRecording()) m_controller->stopRecording();
    else m_controller->startRecording();
    emit recordingChanged();
}

void AppContext::resetView() { m_transform->resetToFit(); }

void AppContext::setVideoSink(QVideoSink* sink) {
    disconnect(m_videoRelay);
    if (m_controller && sink) {
        m_videoRelay = connect(m_controller->displaySink(), &QVideoSink::videoFrameChanged,
                                sink, &QVideoSink::setVideoFrame);
    }
}

void AppContext::openOutputFolder() {
    QDesktopServices::openUrl(QUrl::fromLocalFile(m_outputDir));
}

void AppContext::setStatus(const QString& text) {
    if (m_statusText == text) return;
    m_statusText = text;
    emit statusTextChanged();
}
