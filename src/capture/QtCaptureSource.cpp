#include "capture/QtCaptureSource.h"
#include "device/StopClassification.h"
#include <QDateTime>
#include <QMediaDevices>
#include <algorithm>

namespace {
// QCamera::Error has only NoError and CameraError -- errorOccurred never
// fires with NoError, so branching on the error code cannot distinguish a
// detach from any other failure (device busy, backend failure, bandwidth
// starvation on a shared hub). Whether the device is still enumerated is the
// only reliable signal.
bool isDeviceStillPresent(const QByteArray& id) {
    for (const QCameraDevice& d : QMediaDevices::videoInputs())
        if (d.id() == id) return true;
    return false;
}
} // namespace

QtCaptureSource::QtCaptureSource(QCameraDevice device, QObject* parent)
    : ICaptureSource(parent), m_device(std::move(device)) {

    // The relay is this object's single delivery point; forwarding its
    // signal keeps ICaptureSource's contract ("always emitted on the
    // thread the source lives on") true for every consumer.
    connect(&m_relay, &FrameRelay::frameReady, this, &ICaptureSource::frameReady);

    // Order once through FormatPreference, then rebuild the native list to
    // match. Do NOT try to express the ordering as a pairwise comparator over
    // QCameraFormat -- that is not a strict weak ordering and std::sort is
    // free to do anything with it, including crash.
    const QList<QCameraFormat> native = m_device.videoFormats();

    QList<CaptureFormat> advertised;
    advertised.reserve(native.size());
    for (const QCameraFormat& f : native)
        advertised.append({f.pixelFormat(), f.resolution(), f.maxFrameRate()});

    m_formats = FormatPreference::order(advertised);

    QList<bool> claimed(native.size(), false);
    for (const CaptureFormat& want : m_formats) {
        for (int i = 0; i < native.size(); ++i) {
            if (claimed.at(i)) continue;
            const QCameraFormat& f = native.at(i);
            if (f.pixelFormat() == want.pixelFormat
                && f.resolution() == want.resolution
                && qFuzzyCompare(f.maxFrameRate(), want.maxFrameRate)) {
                m_nativeFormats.append(f);
                claimed[i] = true;
                break;
            }
        }
    }
}

bool QtCaptureSource::start() {
    m_camera  = std::make_unique<QCamera>(m_device);
    m_session = std::make_unique<QMediaCaptureSession>();
    m_sink    = std::make_unique<QVideoSink>();

    m_session->setCamera(m_camera.get());
    m_session->setVideoSink(m_sink.get());
    applySelectedFormat();

    // DirectConnection, deliberately: the lambda runs on whatever thread
    // the backend pushes from, and FrameRelay -- not Qt's event queue --
    // is what decides how that crosses onto this object's thread. With
    // AutoConnection, a backend thread would silently produce one queued
    // event per frame, which is the unbounded queue spec 8.4 forbids (see
    // FrameRelay's header, and Important 7 in the final review).
    connect(m_sink.get(), &QVideoSink::videoFrameChanged, this,
            [this](const QVideoFrame& frame) {
                if (!frame.isValid()) return;
                m_frameSize = frame.size();
                const qint64 ts = frame.startTime() >= 0
                    ? frame.startTime()
                    : QDateTime::currentMSecsSinceEpoch() * 1000;
                m_relay.offer(frame, ts);
            }, Qt::DirectConnection);

    connect(m_camera.get(), &QCamera::errorOccurred, this,
            [this](QCamera::Error error, const QString& detail) {
                Q_UNUSED(error); // always CameraError -- see isDeviceStillPresent above
                // The mapping itself is pure and lives in
                // StopClassification, where it has unit tests that need no
                // hardware; this lambda's only job is to ask the one
                // question the decision rests on.
                emit stopped(StopClassification::forCameraError(
                                 isDeviceStillPresent(m_device.id())),
                             detail);
            });

    m_camera->start();
    return m_camera->isAvailable();
}

void QtCaptureSource::applySelectedFormat() {
    if (!m_camera || m_formatIndex >= m_nativeFormats.size()) return;
    m_camera->setCameraFormat(m_nativeFormats.at(m_formatIndex));
}

bool QtCaptureSource::selectNextFormat() {
    if (m_formatIndex + 1 >= m_nativeFormats.size()) return false;
    ++m_formatIndex;
    applySelectedFormat();
    return true;
}

QStringList QtCaptureSource::formatDescriptions() const {
    QStringList out;
    out.reserve(m_formats.size());
    for (const CaptureFormat& f : m_formats) {
        out.append(QStringLiteral("%1x%2 @ %3fps")
                       .arg(f.resolution.width())
                       .arg(f.resolution.height())
                       .arg(f.maxFrameRate, 0, 'f', 0));
    }
    return out;
}

void QtCaptureSource::stop() {
    if (!m_camera) return;
    m_camera->stop();
    m_camera.reset();
    m_session.reset();
    m_sink.reset();
    emit stopped(StopReason::Requested, QString());
}
