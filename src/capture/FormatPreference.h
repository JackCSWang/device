#pragma once
#include <QList>
#include <QSize>
#include <QVideoFrameFormat>

struct CaptureFormat {
    QVideoFrameFormat::PixelFormat pixelFormat = QVideoFrameFormat::Format_Invalid;
    QSize resolution;
    qreal maxFrameRate = 0.0;
};

// Orders advertised formats best-first. The full list is always returned,
// never filtered, because the watchdog in CaptureController walks it as a
// fallback chain when a format opens but never delivers frames.
class FormatPreference {
public:
    static constexpr int TargetArea = 1920 * 1080;
    static QList<CaptureFormat> order(const QList<CaptureFormat>& advertised);
};
