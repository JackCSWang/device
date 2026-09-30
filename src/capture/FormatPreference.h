#pragma once
#include <QList>
#include <QSize>
#include <QVideoFrameFormat>

struct CaptureFormat {
    QVideoFrameFormat::PixelFormat pixelFormat = QVideoFrameFormat::Format_Invalid;
    QSize resolution;
    qreal maxFrameRate = 0.0;

    // Required so QCOMPARE can diff QList<CaptureFormat> directly (used by
    // Task 10's advertisedFormatsAreOrderedBestFirst); without it the
    // QList<T> QCOMPARE overload fails to compile for lack of operator==.
    bool operator==(const CaptureFormat& other) const {
        return pixelFormat == other.pixelFormat && resolution == other.resolution
            && maxFrameRate == other.maxFrameRate;
    }
};

// Orders advertised formats best-first. The full list is always returned,
// never filtered, because the watchdog in CaptureController walks it as a
// fallback chain when a format opens but never delivers frames.
class FormatPreference {
public:
    static constexpr int TargetArea = 1920 * 1080;
    static QList<CaptureFormat> order(const QList<CaptureFormat>& advertised);

    // The ordering predicate itself: "a is preferable to b". Exposed so the
    // strict-weak-ordering property can be tested directly -- a comparator
    // that is not a valid strict weak ordering lets std::sort do anything
    // at all, including crash, and nothing about the sorted *output* of a
    // handful of example lists would reveal it.
    static bool isBetter(const CaptureFormat& a, const CaptureFormat& b);

    // Exposed for the same reason: the cap is the one non-obvious term in
    // the ordering, and a test that recomputes it from TargetArea itself
    // stays honest if the target ever changes.
    static int cappedArea(const QSize& s);
};
