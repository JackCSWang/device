#pragma once
#include "core/ICaptureSource.h"
#include <QColor>
#include <QVideoFrameFormat>

// Synthetic source for tests and CI. Emits a flat frame in a known colour
// that advances each frame, so a consumer can assert exact pixels.
//
// Failure injection exists so every row of spec 10.3 gets an automated test
// with no scope attached.
class FakeCaptureSource : public ICaptureSource {
    Q_OBJECT
public:
    explicit FakeCaptureSource(QObject* parent = nullptr);

    bool start() override;
    void stop() override;
    QSize frameSize() const override { return m_size; }
    bool selectNextFormat() override;

    void setFrameSize(QSize size) { m_size = size; }
    void setPixelFormat(QVideoFrameFormat::PixelFormat f) { m_pixelFormat = f; }
    void setDeliverFrames(bool on) { m_deliver = on; }

    // How many times selectNextFormat() may succeed before the chain is
    // exhausted. Lets the watchdog's fallback path be tested end to end.
    void setFallbackCount(int n) { m_fallbacksLeft = n; }
    int fallbacksUsed() const { return m_fallbacksUsed; }

    // After emitting, overwrite the frame buffer. Any consumer that failed to
    // deep-copy will see corruption, and its test will fail.
    void setScribbleAfterEmit(bool on) { m_scribble = on; }

    void emitOneFrame();
    void injectDetach();

    QColor nextFillColor() const;

private:
    void fillRgb(QVideoFrame& frame, const QColor& c) const;
    void fillYuyv(QVideoFrame& frame, const QColor& c) const;

    QSize m_size{640, 480};
    QVideoFrameFormat::PixelFormat m_pixelFormat = QVideoFrameFormat::Format_RGBX8888;
    bool m_running = false;
    bool m_deliver = true;
    bool m_scribble = false;
    int m_counter = 0;
    int m_fallbacksLeft = 0;
    int m_fallbacksUsed = 0;
    qint64 m_tsUs = 0;
};
