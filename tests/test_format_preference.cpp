#include <QtTest>
#include "capture/FormatPreference.h"

using PF = QVideoFrameFormat::PixelFormat;

class TestFormatPreference : public QObject {
    Q_OBJECT
    static CaptureFormat f(PF p, int w, int h, qreal fps = 30.0) {
        return CaptureFormat{p, QSize(w, h), fps};
    }
private slots:
    void prefersMjpegAtTenEightyOverEverything() {
        const auto out = FormatPreference::order({
            f(PF::Format_YUYV, 1280, 720),
            f(PF::Format_Jpeg, 1920, 1080),
            f(PF::Format_YUYV, 640, 480)});
        QCOMPARE(out.first().pixelFormat, PF::Format_Jpeg);
        QCOMPARE(out.first().resolution, QSize(1920, 1080));
    }

    void prefersYuyvOverUnknownFormats() {
        const auto out = FormatPreference::order({
            f(PF::Format_NV12, 1920, 1080),
            f(PF::Format_YUYV, 1280, 720)});
        QCOMPARE(out.first().pixelFormat, PF::Format_YUYV);
    }

    void prefersLargerResolutionUpToTenEighty() {
        const auto out = FormatPreference::order({
            f(PF::Format_Jpeg, 640, 480),
            f(PF::Format_Jpeg, 1280, 720)});
        QCOMPARE(out.first().resolution, QSize(1280, 720));
    }

    // Beyond 1080p there is no benefit, and bandwidth risk rises (spec 10.2).
    void doesNotPreferAboveTenEighty() {
        const auto out = FormatPreference::order({
            f(PF::Format_Jpeg, 3840, 2160),
            f(PF::Format_Jpeg, 1920, 1080)});
        QCOMPARE(out.first().resolution, QSize(1920, 1080));
    }

    void breaksTiesByFrameRate() {
        const auto out = FormatPreference::order({
            f(PF::Format_Jpeg, 1920, 1080, 15.0),
            f(PF::Format_Jpeg, 1920, 1080, 30.0)});
        QCOMPARE(out.first().maxFrameRate, 30.0);
    }

    void keepsEveryFormatSoFallbackAlwaysHasSomethingToTry() {
        const QList<CaptureFormat> in{
            f(PF::Format_NV12, 320, 240),
            f(PF::Format_Jpeg, 1920, 1080),
            f(PF::Format_YUYV, 1280, 720)};
        QCOMPARE(FormatPreference::order(in).size(), in.size());
    }

    void handlesEmptyInput() {
        QVERIFY(FormatPreference::order({}).isEmpty());
    }
};

QTEST_MAIN(TestFormatPreference)
#include "test_format_preference.moc"
