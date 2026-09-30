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

int actualArea(const QSize& s) { return s.width() * s.height(); }
} // namespace

// Area, but capped: above 1080p there is no inspection benefit and
// isochronous bandwidth risk rises (spec 10.2).
int FormatPreference::cappedArea(const QSize& s) {
    return std::min(actualArea(s), TargetArea);
}

bool FormatPreference::isBetter(const CaptureFormat& a, const CaptureFormat& b) {
    // Resolution outranks pixel format.
    //
    // It used to be the other way round, which contradicted spec 8.5's own
    // paired preference list -- "MJPEG 1080p -> YUY2 720p -> first
    // supported". Every entry there pairs a format with a resolution, and
    // the fallback steps *down* in both at once; nothing in it says a
    // better-compressed format at a far lower resolution is preferable. A
    // scope advertising MJPEG 640x480 alongside YUYV 1280x720 opened at
    // 0.3 MP, on a tool whose entire purpose is looking closely at things.
    //
    // Pixel format still breaks the tie, and that is where it earns its
    // keep: at equal resolution MJPEG costs the least bus bandwidth, which
    // is what spec 10.2's shared-hub failure is about.
    const int aa = cappedArea(a.resolution), ab = cappedArea(b.resolution);
    if (aa != ab) return aa > ab;

    const int ra = formatRank(a.pixelFormat), rb = formatRank(b.pixelFormat);
    if (ra != rb) return ra > rb;

    if (a.maxFrameRate != b.maxFrameRate) return a.maxFrameRate > b.maxFrameRate;

    // When capped area, format and frame rate are all equal, prefer the
    // smaller actual resolution, so 1920x1080 is not passed over for
    // 3840x2160 (which the cap has made indistinguishable on area).
    return actualArea(a.resolution) < actualArea(b.resolution);
}

QList<CaptureFormat> FormatPreference::order(const QList<CaptureFormat>& advertised) {
    QList<CaptureFormat> out = advertised;
    std::stable_sort(out.begin(), out.end(), isBetter);
    return out;
}
