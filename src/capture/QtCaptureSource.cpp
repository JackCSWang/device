#include "capture/QtCaptureSource.h"
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

    connect(m_sink.get(), &QVideoSink::videoFrameChanged, this,
            [this](const QVideoFrame& frame) {
                if (!frame.isValid()) return;
                m_frameSize = frame.size();
                const qint64 ts = frame.startTime() >= 0
                    ? frame.startTime()
                    : QDateTime::currentMSecsSinceEpoch() * 1000;
                emit frameReady(frame, ts);
            });

    connect(m_camera.get(), &QCamera::errorOccurred, this,
            [this](QCamera::Error error, const QString& detail) {
                Q_UNUSED(error); // always CameraError -- see isDeviceStillPresent above
                if (isDeviceStillPresent(m_device.id()))
                    emit stopped(StopReason::Error, detail);
                else
                    emit stopped(StopReason::Detached, detail);
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

void QtCaptureSource::stop() {
    if (!m_camera) return;
    m_camera->stop();
    m_camera.reset();
    m_session.reset();
    m_sink.reset();
    emit stopped(StopReason::Requested, QString());
}
