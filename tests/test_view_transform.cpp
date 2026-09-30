#include <QtTest>
#include "view/ViewTransform.h"

class TestViewTransform : public QObject {
    Q_OBJECT

    ViewTransform wide() {           // 16:9 frame, 16:9 viewport
        ViewTransform t;
        t.setFrameSize({1920, 1080});
        t.setViewportSize({1280, 720});
        return t;
    }
    ViewTransform square() {         // 16:9 frame, square viewport (letterboxed)
        ViewTransform t;
        t.setFrameSize({1920, 1080});
        t.setViewportSize({1000, 1000});
        return t;
    }

private slots:
    void defaultsToFit() {
        auto t = wide();
        QCOMPARE(t.zoom(), 1.0);
        QCOMPARE(t.pan(), QPointF(0, 0));
        QCOMPARE(t.visibleFrameRect(), QRectF(0, 0, 1920, 1080));
    }

    void zoomClampsToMax() {
        auto t = wide();
        t.zoomAt(100.0, {640, 360});
        QCOMPARE(t.zoom(), ViewTransform::MaxZoom);
    }

    void zoomClampsToMin() {
        auto t = wide();
        t.zoomAt(0.01, {640, 360});
        QCOMPARE(t.zoom(), ViewTransform::MinZoom);
    }

    void cannotPanAtFit() {
        auto t = wide();
        t.panBy({5000, 5000});
        QCOMPARE(t.pan(), QPointF(0, 0));
    }

    void panClampsToFrameEdge() {
        auto t = wide();
        t.zoomAt(4.0, {640, 360});          // visible = 480 x 270
        t.panBy({-99999, -99999});          // drag content far left/up
        const QRectF r = t.visibleFrameRect();
        QCOMPARE(r.right(), 1920.0);
        QCOMPARE(r.bottom(), 1080.0);
    }

    // Review Focus 1: letterboxed axis must centre, not clamp to an edge.
    void letterboxedAxisStaysCentred() {
        auto t = square();
        QCOMPARE(t.zoom(), 1.0);
        t.panBy({0, 800});
        QCOMPARE(t.pan().y(), 0.0);                 // no vertical freedom
        const QRectF r = t.visibleFrameRect();
        QCOMPARE(r.left(), 0.0);
        QCOMPARE(r.width(), 1920.0);                // full width visible
        QVERIFY(r.height() > 1080.0);               // region exceeds frame
        QCOMPARE(r.center().y(), 540.0);            // centred on the frame
    }

    void letterboxedPansOnlyWhenZoomedIn() {
        auto t = square();
        t.zoomAt(4.0, {500, 500});   // s = (1000/1920)*4; visible = 480 x 480
        t.panBy({0, -99999});
        const QRectF r = t.visibleFrameRect();
        QCOMPARE(r.bottom(), 1080.0);
        QCOMPARE(r.height(), 480.0);
    }

    void zoomKeepsFocusPointFixed() {
        auto t = wide();
        const QPointF focus{300, 200};
        const QRectF before = t.visibleFrameRect();
        const qreal sBefore = 1280.0 / before.width();
        const QPointF frameAtFocusBefore =
            before.topLeft() + QPointF(focus.x() / sBefore, focus.y() / sBefore);

        t.zoomAt(2.0, focus);

        const QRectF after = t.visibleFrameRect();
        const qreal sAfter = 1280.0 / after.width();
        const QPointF frameAtFocusAfter =
            after.topLeft() + QPointF(focus.x() / sAfter, focus.y() / sAfter);

        QVERIFY(qAbs(frameAtFocusBefore.x() - frameAtFocusAfter.x()) < 0.5);
        QVERIFY(qAbs(frameAtFocusBefore.y() - frameAtFocusAfter.y()) < 0.5);
    }

    void resetToFitRestoresDefaults() {
        auto t = wide();
        t.zoomAt(6.0, {100, 100});
        t.panBy({-200, -100});
        t.resetToFit();
        QCOMPARE(t.zoom(), 1.0);
        QCOMPARE(t.pan(), QPointF(0, 0));
    }
};

QTEST_MAIN(TestViewTransform)
#include "test_view_transform.moc"
