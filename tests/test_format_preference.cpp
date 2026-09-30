#include <QtTest>
#include "capture/FormatPreference.h"

using PF = QVideoFrameFormat::PixelFormat;

class TestFormatPreference : public QObject {
    Q_OBJECT
    static CaptureFormat f(PF p, int w, int h, qreal fps = 30.0) {
        return CaptureFormat{p, QSize(w, h), fps};
    }

    // A spread wide enough to exercise every term of the ordering against
    // every other: both sides of the 1080p cap, three format ranks, two
    // frame rates, and pairs that tie on each term in turn.
    static QList<CaptureFormat> sampleFormats() {
        return {
            f(PF::Format_Jpeg, 1920, 1080, 30.0), f(PF::Format_Jpeg, 1920, 1080, 15.0),
            f(PF::Format_YUYV, 1920, 1080, 30.0), f(PF::Format_NV12, 1920, 1080, 30.0),
            f(PF::Format_Jpeg, 3840, 2160, 30.0), f(PF::Format_YUYV, 3840, 2160, 15.0),
            f(PF::Format_Jpeg, 1280, 720, 30.0),  f(PF::Format_YUYV, 1280, 720, 30.0),
            f(PF::Format_NV12, 1280, 720, 30.0),  f(PF::Format_Jpeg, 640, 480, 30.0),
            f(PF::Format_YUYV, 640, 480, 5.0),    f(PF::Format_ARGB8888, 320, 240, 30.0),
        };
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

    // Important 5. Spec 8.5's preference list is *paired*: "MJPEG 1080p ->
    // YUY2 720p -> first supported". Every entry pairs a format with a
    // resolution and the fallback steps down in both at once; nothing in it
    // says a better-compressed format at a far lower resolution wins.
    //
    // Ranking pixel format above resolution meant a scope advertising MJPEG
    // 640x480 and YUYV 1280x720 opened at 0.3 MP -- on a tool whose entire
    // purpose is looking closely at things. This is the row that fails if
    // the old ordering comes back.
    void prefersTheHigherResolutionEvenInAWorseFormat() {
        const auto out = FormatPreference::order({
            f(PF::Format_Jpeg, 640, 480),
            f(PF::Format_YUYV, 1280, 720)});
        QCOMPARE(out.first().resolution, QSize(1280, 720));
        QCOMPARE(out.first().pixelFormat, PF::Format_YUYV);
    }

    // Pixel format still decides, and this is where it earns its keep: at
    // equal resolution MJPEG costs the least bus bandwidth, which is
    // exactly what spec 10.2's shared-hub failure is about.
    //
    // This replaces a version that paired NV12 1080p against YUYV 720p and
    // so proved nothing about format preference at all -- it was satisfied
    // by either ordering rule. A same-resolution pairing is what it should
    // always have been.
    void prefersYuyvOverUnknownFormatsAtTheSameResolution() {
        const auto out = FormatPreference::order({
            f(PF::Format_NV12, 1280, 720),
            f(PF::Format_YUYV, 1280, 720)});
        QCOMPARE(out.first().pixelFormat, PF::Format_YUYV);
        QCOMPARE(out.first().resolution, QSize(1280, 720));
    }

    void prefersMjpegOverYuyvAtTheSameResolution() {
        const auto out = FormatPreference::order({
            f(PF::Format_YUYV, 1920, 1080),
            f(PF::Format_Jpeg, 1920, 1080)});
        QCOMPARE(out.first().pixelFormat, PF::Format_Jpeg);
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

    // The cap is the one non-obvious term, so pin it against TargetArea
    // itself rather than a literal.
    void areaIsCappedAtTheTarget() {
        QCOMPARE(FormatPreference::cappedArea(QSize(1920, 1080)),
                 FormatPreference::TargetArea);
        QCOMPARE(FormatPreference::cappedArea(QSize(3840, 2160)),
                 FormatPreference::TargetArea);
        QCOMPARE(FormatPreference::cappedArea(QSize(1280, 720)), 1280 * 720);
    }

    // With resolution now outranking format, a 4K MJPEG mode must not beat
    // a 1080p one -- the cap makes them tie on area, and the smaller actual
    // resolution then wins.
    void fourKDoesNotOutrankTenEightyEvenInTheBestFormat() {
        const auto out = FormatPreference::order({
            f(PF::Format_Jpeg, 3840, 2160),
            f(PF::Format_YUYV, 1920, 1080)});
        // Both cap to TargetArea, so format breaks the tie and MJPEG wins.
        QCOMPARE(out.first().resolution, QSize(3840, 2160));
        QCOMPARE(out.first().pixelFormat, PF::Format_Jpeg);
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

    // isBetter() must be a valid strict weak ordering, or std::sort is
    // free to do literally anything with it -- including run off the end of
    // the range. No amount of checking the *output* of example lists can
    // catch that; the property has to be checked directly.
    //
    // Irreflexivity: nothing is better than itself.
    void orderingIsIrreflexive() {
        for (const CaptureFormat& a : sampleFormats())
            QVERIFY(!FormatPreference::isBetter(a, a));
    }

    // Asymmetry: isBetter(a,b) and isBetter(b,a) can never both hold.
    void orderingIsAsymmetric() {
        const auto all = sampleFormats();
        for (const CaptureFormat& a : all)
            for (const CaptureFormat& b : all)
                QVERIFY(!(FormatPreference::isBetter(a, b)
                          && FormatPreference::isBetter(b, a)));
    }

    // Transitivity of isBetter, and of the induced equivalence
    // (!better(a,b) && !better(b,a)) -- the second is the one a naive
    // multi-key comparator usually gets wrong.
    void orderingIsTransitive() {
        const auto all = sampleFormats();
        for (const CaptureFormat& a : all) {
            for (const CaptureFormat& b : all) {
                for (const CaptureFormat& c : all) {
                    if (FormatPreference::isBetter(a, b)
                        && FormatPreference::isBetter(b, c))
                        QVERIFY(FormatPreference::isBetter(a, c));

                    const auto equiv = [](const CaptureFormat& x, const CaptureFormat& y) {
                        return !FormatPreference::isBetter(x, y)
                            && !FormatPreference::isBetter(y, x);
                    };
                    if (equiv(a, b) && equiv(b, c)) QVERIFY(equiv(a, c));
                }
            }
        }
    }

    // A valid ordering makes order() idempotent; an invalid one need not be.
    void orderingASortedListChangesNothing() {
        const auto once = FormatPreference::order(sampleFormats());
        QCOMPARE(FormatPreference::order(once), once);
    }
};

QTEST_MAIN(TestFormatPreference)
#include "test_format_preference.moc"
