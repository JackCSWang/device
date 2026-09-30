#include "AppContext.h"
#include "capture/CaptureSession.h"
#include "capture/QtCaptureSource.h"
#include "capture/StatusModel.h"
#include "device/DeviceRegistry.h"
#include "output/QtRecorder.h"
#include "storage/OutputLocation.h"
#include "view/ViewTransformModel.h"
#include <QDesktopServices>
#include <QSettings>
#include <QUrl>
#include <QVideoSink>

namespace {
// Where the remembered scope id is persisted. Persistence is wiring; the
// *policy* of preferring a remembered device lives in DeviceSelection,
// which has tests.
constexpr auto kRememberedDeviceKey = "device/lastUsedId";

QSettings appSettings() {
    return QSettings(QStringLiteral("WTC"), QStringLiteral("Microscope"));
}
} // namespace

AppContext::AppContext(QObject* parent)
    : QObject(parent),
      m_registry(new DeviceRegistry(this)),
      m_transform(new ViewTransformModel(this)),
      m_outputDir(OutputLocation::defaultDirectory()) {

    m_session = std::make_unique<CaptureSession>(
        m_registry,
        [](const ScopeDevice& device) -> std::unique_ptr<ICaptureSource> {
            return std::make_unique<QtCaptureSource>(device.device);
        },
        []() -> std::unique_ptr<IRecorder> { return std::make_unique<QtRecorder>(); },
        m_outputDir);

    connect(m_session.get(), &CaptureSession::pipelineChanged,
            this, &AppContext::pipelineChanged);
    connect(m_session.get(), &CaptureSession::recordingChanged,
            this, &AppContext::recordingChanged);
    connect(m_session.get(), &CaptureSession::deviceListChanged,
            this, &AppContext::deviceListChanged);
    connect(m_session->status(), &StatusModel::deviceStatusChanged,
            this, &AppContext::statusTextChanged);
    connect(m_session->status(), &StatusModel::lastOutcomeChanged,
            this, &AppContext::lastOutcomeTextChanged);
    connect(m_session.get(), &CaptureSession::frameSizeChanged, this, [this](QSize size) {
        m_transform->setFrameSize(size);
    });
    connect(m_session.get(), &CaptureSession::rememberedDeviceIdChanged,
            this, [](const QString& id) {
                appSettings().setValue(QLatin1String(kRememberedDeviceKey), id);
            });

    m_session->setRememberedDeviceId(
        appSettings().value(QLatin1String(kRememberedDeviceKey)).toString());
    m_session->start();
}

AppContext::~AppContext() = default;

QString AppContext::statusText() const { return m_session->status()->deviceStatus(); }
QString AppContext::lastOutcomeText() const { return m_session->status()->lastOutcome(); }
bool AppContext::recording() const { return m_session->isRecording(); }
bool AppContext::hasDevice() const { return m_session->hasDevice(); }
bool AppContext::hasVideo() const { return m_session->hasVideo(); }
bool AppContext::cameraAccessDenied() const { return m_session->cameraAccessDenied(); }
bool AppContext::needsDeviceChoice() const { return m_session->needsDeviceChoice(); }
QStringList AppContext::deviceNames() const { return m_session->deviceDescriptions(); }

void AppContext::snapshot() { m_session->takeSnapshot(); }
void AppContext::toggleRecording() { m_session->toggleRecording(); }
void AppContext::resetView() { m_transform->resetToFit(); }
void AppContext::selectDevice(int index) { m_session->selectDevice(index); }
void AppContext::retry() { m_session->retry(); }

void AppContext::setVideoSink(QVideoSink* sink) {
    disconnect(m_videoRelay);
    if (QVideoSink* display = m_session->displaySink(); display && sink) {
        m_videoRelay = connect(display, &QVideoSink::videoFrameChanged,
                               sink, &QVideoSink::setVideoFrame);
    }
}

void AppContext::openOutputFolder() {
    QDesktopServices::openUrl(QUrl::fromLocalFile(m_outputDir));
}

void AppContext::openCameraPrivacySettings() {
#ifdef Q_OS_MACOS
    QDesktopServices::openUrl(QUrl(QStringLiteral(
        "x-apple.systempreferences:com.apple.preference.security?Privacy_Camera")));
#elif defined(Q_OS_WIN)
    QDesktopServices::openUrl(QUrl(QStringLiteral("ms-settings:privacy-webcam")));
#else
    m_session->status()->setDeviceStatus(
        tr("Run: sudo usermod -aG video $USER   then log out and back in."));
#endif
}
