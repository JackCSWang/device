#pragma once
#include "capture/FormatPreference.h"
#include "capture/FrameRelay.h"
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

    // Exposed for the hardware test that records what the backend actually
    // does: coalescedCount() stays 0 for as long as delivery is
    // same-thread, which is what it is on Windows.
    const FrameRelay& relay() const { return m_relay; }

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

    // Every frame from the backend goes through here, so frameReady is
    // always emitted on this object's own thread with at most one frame
    // ever pending (spec 8.4). Declared before m_camera so it outlives the
    // sink whose callbacks feed it.
    FrameRelay m_relay;

    QCameraDevice m_device;
    std::unique_ptr<QCamera> m_camera;
    std::unique_ptr<QMediaCaptureSession> m_session;
    std::unique_ptr<QVideoSink> m_sink;
    QList<CaptureFormat> m_formats;
    QList<QCameraFormat> m_nativeFormats;
    int m_formatIndex = 0;
    QSize m_frameSize;
};
