#include <QtTest>
#include <QSignalSpy>
#include "view/ViewTransformModel.h"

class TestViewTransformModel : public QObject {
    Q_OBJECT
    static ViewTransformModel* make(QObject* parent) {
        auto* m = new ViewTransformModel(parent);
        m->setFrameSize({1920, 1080});
        m->setViewportSize({1280, 720});
        return m;
    }
private slots:
    void startsAtFit() {
        QObject owner; auto* m = make(&owner);
        QCOMPARE(m->zoom(), 1.0);
        QCOMPARE(m->contentX(), 0.0);
        QCOMPARE(m->contentY(), 0.0);
        QVERIFY(!m->canPan());
    }
    void zoomingEmitsChangedOnce() {
        QObject owner; auto* m = make(&owner);
        QSignalSpy spy(m, &ViewTransformModel::changed);
        m->zoomAt(2.0, 640, 360);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(m->zoom(), 2.0);
        QVERIFY(m->canPan());
    }
    void contentScaleGrowsWithZoom() {
        QObject owner; auto* m = make(&owner);
        const qreal before = m->contentScale();
        m->zoomAt(4.0, 640, 360);
        QVERIFY(m->contentScale() > before * 3.5);
    }
    void resetEmitsChangedAndRestoresFit() {
        QObject owner; auto* m = make(&owner);
        m->zoomAt(4.0, 100, 100);
        m->panBy(-50, -20);
        QSignalSpy spy(m, &ViewTransformModel::changed);
        m->resetToFit();
        QCOMPARE(spy.count(), 1);
        QCOMPARE(m->zoom(), 1.0);
        QCOMPARE(m->contentX(), 0.0);
    }
    void viewportResizeReclampsPanOnAspectChange() {
        QObject owner; auto* m = make(&owner);
        // Frame: 1920x1080, Viewport: 1280x720 (16:9, same aspect as frame)

        // Zoom to 8.0 maximum
        m->zoomAt(8.0, 640, 360);

        // At zoom 8.0 with 1280x720 viewport:
        // fitScale = min(1280/1920, 720/1080) = 0.667
        // scale = 0.667 * 8 = 5.333
        // visible = (240, 135)
        // maxPanY = (1080 - 135) / 2 = 472.5

        // Pan downward (positive viewport Y = down)
        // At zoom 8.0 with 1280x720, maxPanY = 472.5 in frame coords
        // In viewport coords that's 472.5 * 5.333 ≈ 2520
        // Pan toward the bottom edge
        m->panBy(0.0, 2400.0);

        // Now resize viewport to 400x720 (tall, no longer matching frame aspect)
        // This CHANGES the aspect ratio, which changes visible bounds:
        // fitScale = min(400/1920, 720/1080) = 0.208
        // scale = 0.208 * 8 = 1.667
        // visible = (240, 432)
        // maxPanY = (1080 - 432) / 2 = 324 (much smaller!)
        m->setViewportSize({400, 720});

        // If clampPan() was called, pan is reclamped from 472.5 to 324
        // contentY should reflect the new, smaller pan bound
        const qreal contentYAfter = m->contentY();

        // If clampPan() WAS called:
        //   pan reclamped from -469 to -324
        //   contentY = -(-324) * 1.667 ≈ 540
        // If clampPan() was NOT called:
        //   pan stays at -469
        //   contentY = -(-469) * 1.667 ≈ 782

        // The key assertion: contentY must be the reclamped value (≈540)
        // not the unclamped value (≈782)
        // Fuzzy compare with tolerance
        QVERIFY(qAbs(contentYAfter - 540.0) < 50.0);
    }
};

QTEST_MAIN(TestViewTransformModel)
#include "test_view_transform_model.moc"
