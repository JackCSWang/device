#include <QtTest>
#include <QImage>
#include "view/Orientation.h"

namespace {
// A 4x2 probe with a single white pixel at the top-left corner. Asymmetric
// in BOTH axes on purpose: a square image, or a marker on a centre line,
// would make a flipped rotation direction or a reversed mirror/rotate order
// indistinguishable from the correct result.
QImage probe() {
    QImage img(4, 2, QImage::Format_RGB32);
    img.fill(Qt::black);
    img.setPixelColor(0, 0, Qt::white);
    return img;
}

// Position of the single white pixel, or (-1,-1) if it was lost.
QPoint marker(const QImage& img) {
    for (int y = 0; y < img.height(); ++y)
        for (int x = 0; x < img.width(); ++x)
            if (img.pixelColor(x, y) == QColor(Qt::white))
                return {x, y};
    return {-1, -1};
}
}

class TestOrientation : public QObject {
    Q_OBJECT
private slots:
    void defaultsToIdentityAndTouchesNothing() {
        Orientation o;
        QVERIFY(o.isIdentity());
        QCOMPARE(o.degrees(), 0);
        QVERIFY(!o.mirrored());
        QCOMPARE(o.transformedSize(QSize(640, 480)), QSize(640, 480));

        const QImage in = probe();
        const QImage out = o.apply(in);
        QCOMPARE(out, in);
        QCOMPARE(marker(out), QPoint(0, 0));
    }

    void quarterTurnsCycleAndWrap() {
        Orientation o;
        o.rotateClockwise();          QCOMPARE(o.degrees(), 90);
        o.rotateClockwise();          QCOMPARE(o.degrees(), 180);
        o.rotateClockwise();          QCOMPARE(o.degrees(), 270);
        o.rotateClockwise();          QCOMPARE(o.degrees(), 0);
        QVERIFY(o.isIdentity());

        o.rotateCounterClockwise();   QCOMPARE(o.degrees(), 270);
        o.rotateCounterClockwise();   QCOMPARE(o.degrees(), 180);
    }

    // Pins the DIRECTION of rotation, not merely that something rotated.
    // Clockwise must carry the top-left corner to the top-right; if the two
    // handlers were ever swapped this is the test that fails.
    void clockwiseCarriesTopLeftToTopRight() {
        Orientation o;
        o.rotateClockwise();
        QCOMPARE(o.transformedSize(QSize(4, 2)), QSize(2, 4));

        const QImage out = o.apply(probe());
        QCOMPARE(out.size(), QSize(2, 4));
        QCOMPARE(marker(out), QPoint(1, 0));      // top-right
    }

    void counterClockwiseCarriesTopLeftToBottomLeft() {
        Orientation o;
        o.rotateCounterClockwise();
        const QImage out = o.apply(probe());
        QCOMPARE(out.size(), QSize(2, 4));
        QCOMPARE(marker(out), QPoint(0, 3));      // bottom-left
    }

    void mirrorIsHorizontalAndKeepsSize() {
        Orientation o;
        o.toggleMirror();
        QVERIFY(o.mirrored());
        QVERIFY(!o.isIdentity());
        QCOMPARE(o.transformedSize(QSize(4, 2)), QSize(4, 2));

        const QImage out = o.apply(probe());
        QCOMPARE(out.size(), QSize(4, 2));
        QCOMPARE(marker(out), QPoint(3, 0));      // flipped across x only

        o.toggleMirror();
        QVERIFY(o.isIdentity());
    }

    // THE ORDER TEST. apply() must mirror first and rotate second. With
    // mirror+90 the marker lands bottom-right (1,3); the reversed order
    // would put it top-left (0,0). Both are "rotated and mirrored", so only
    // an exact position assertion can tell them apart -- and the live view
    // and the saved file disagreeing by a 180-degree turn is precisely the
    // bug this pins.
    void appliesMirrorBeforeRotation() {
        Orientation o;
        o.toggleMirror();
        o.rotateClockwise();

        const QImage out = o.apply(probe());
        QCOMPARE(out.size(), QSize(2, 4));
        QCOMPARE(marker(out), QPoint(1, 3));      // bottom-right
    }

    // Button order must not matter: the state is (degrees, mirrored), and
    // apply() imposes the order. Pressing rotate-then-mirror and
    // mirror-then-rotate must produce identical pixels.
    void buttonPressOrderDoesNotChangeResult() {
        Orientation a; a.toggleMirror();      a.rotateClockwise();
        Orientation b; b.rotateClockwise();   b.toggleMirror();
        QCOMPARE(a.degrees(), b.degrees());
        QCOMPARE(a.mirrored(), b.mirrored());
        QCOMPARE(a.apply(probe()), b.apply(probe()));
    }

    void rotationIsLosslessAndKeepsEveryPixel() {
        Orientation o;
        o.rotateClockwise();
        const QImage in = probe();
        const QImage out = o.apply(in);
        // Same pixel count: orientation crops nothing. This is the property
        // that lets orientation be baked into evidence while zoom cannot be.
        QCOMPARE(out.width() * out.height(), in.width() * in.height());
        QVERIFY(marker(out) != QPoint(-1, -1));   // the marker survived
    }

    void resetReturnsToIdentity() {
        Orientation o;
        o.rotateClockwise();
        o.toggleMirror();
        QVERIFY(!o.isIdentity());
        o.reset();
        QVERIFY(o.isIdentity());
        QCOMPARE(o.apply(probe()), probe());
    }

    void fourQuarterTurnsRestoreTheOriginalImage() {
        Orientation o;
        for (int i = 0; i < 4; ++i) o.rotateClockwise();
        QCOMPARE(o.apply(probe()), probe());
    }
};

QTEST_MAIN(TestOrientation)
#include "test_orientation.moc"
