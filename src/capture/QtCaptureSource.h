#pragma once
#include "capture/FormatPreference.h"
#include "core/ICaptureSource.h"
#include <QCamera>
#include <QCameraDevice>
#include <QMediaCaptureSession>
#include <QVideoSink>
#include <memory>

// Desktop implementation: Windows Media Foundation, macOS AVFoundation, and
// Linux V4L2, all via one QCamera. No native code and no driver.
class QtCaptureSource : public ICaptureSource {
    Q_OBJECT
public:
    explicit QtCaptureSource(QCameraDevice device, QObject* parent = nullptr);

    bool start() override;
    void stop() override;
    QSize frameSize() const override { return m_frameSize; }

    QList<CaptureFormat> advertisedFormats() const { return m_formats; }

    // Advances to the next format in the preference chain. Returns false when
    // the chain is exhausted -- the watchdog uses this on a silent open.
    bool selectNextFormat() override;

    QStringList formatDescriptions() const override;

    // On Linux (V4L2) QCameraDevice::id() *is* the `/dev/videoN` path, which
    // is what spec 10.3's EACCES row needs. On Windows and macOS it is an
    // opaque handle; the caller checks for a path before using it.
    QString deviceNode() const override { return QString::fromUtf8(m_device.id()); }

private:
    void applySelectedFormat();

    QCameraDevice m_device;
    std::unique_ptr<QCamera> m_camera;
    std::unique_ptr<QMediaCaptureSession> m_session;
    std::unique_ptr<QVideoSink> m_sink;
    QList<CaptureFormat> m_formats;
    QList<QCameraFormat> m_nativeFormats;
    int m_formatIndex = 0;
    QSize m_frameSize;
};
