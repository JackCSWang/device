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
