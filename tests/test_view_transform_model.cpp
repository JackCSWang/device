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
    void viewportResizeKeepsZoomValid() {
        QObject owner; auto* m = make(&owner);
        m->zoomAt(8.0, 640, 360);
        m->setViewportSize({400, 400});
        QVERIFY(m->zoom() <= ViewTransform::MaxZoom);
        QVERIFY(m->zoom() >= ViewTransform::MinZoom);
    }
};

QTEST_MAIN(TestViewTransformModel)
#include "test_view_transform_model.moc"
