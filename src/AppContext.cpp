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

    connect(m_registry, &DeviceRegistry::attached, this, [this](const ScopeDevice&) {
        if (!m_source) openFirstAvailableDevice();
    });
    connect(m_registry, &DeviceRegistry::detached, this, [this](const QString&) {
        emit pipelineChanged();
    });

    m_registry->watch();
    openFirstAvailableDevice();
}

AppContext::~AppContext() = default;

void AppContext::openFirstAvailableDevice() {
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
        m_controller.reset(); m_source.reset(); m_recorder.reset();
        emit pipelineChanged();
        emit recordingChanged();
    });
    connect(m_recorder.get(), &IRecorder::finished,
            this, &AppContext::recordingChanged);

    connect(m_source.get(), &ICaptureSource::frameReady, this,
            [this](const QVideoFrame& f, qint64) {
                m_transform->setFrameSize(f.size());
            });

    m_controller->begin();
    setStatus(tr("Connected to %1.").arg(devices.first().description));
    emit pipelineChanged();
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
