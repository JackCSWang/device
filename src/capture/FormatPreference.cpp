#include "capture/FormatPreference.h"
#include <algorithm>

namespace {
int formatRank(QVideoFrameFormat::PixelFormat p) {
    switch (p) {
    case QVideoFrameFormat::Format_Jpeg: return 2;   // MJPEG: least bus bandwidth
    case QVideoFrameFormat::Format_YUYV: return 1;   // uncompressed fallback
    default:                             return 0;
    }
}

// Area, but capped: above 1080p there is no inspection benefit and
// isochronous bandwidth risk rises (spec 10.2).
int cappedArea(const QSize& s) {
    return std::min(s.width() * s.height(), FormatPreference::TargetArea);
}
} // namespace

QList<CaptureFormat> FormatPreference::order(const QList<CaptureFormat>& advertised) {
    QList<CaptureFormat> out = advertised;
    std::stable_sort(out.begin(), out.end(),
                     [](const CaptureFormat& a, const CaptureFormat& b) {
        const int ra = formatRank(a.pixelFormat), rb = formatRank(b.pixelFormat);
        if (ra != rb) return ra > rb;
        const int aa = cappedArea(a.resolution), ab = cappedArea(b.resolution);
        if (aa != ab) return aa > ab;
        if (a.maxFrameRate != b.maxFrameRate) return a.maxFrameRate > b.maxFrameRate;
        // When capped area and frame rate are equal, prefer smaller actual resolution.
        // This ensures we don't prefer 3840x2160 over 1920x1080 when both have the same capped area.
        return a.resolution.width() * a.resolution.height() < b.resolution.width() * b.resolution.height();
    });
    return out;
}
