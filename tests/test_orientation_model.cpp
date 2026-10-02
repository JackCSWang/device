#include <QtTest>
#include <QSignalSpy>
#include "view/OrientationModel.h"

class TestOrientationModel : public QObject {
    Q_OBJECT
private slots:
    void startsIdentityAndUnlocked() {
        OrientationModel m;
        QCOMPARE(m.degrees(), 0);
        QVERIFY(!m.mirrored());
        QVERIFY(m.isIdentity());
        QVERIFY(!m.locked());
    }

    void eachMutationNotifiesExactlyOnce() {
        OrientationModel m;
        QSignalSpy spy(&m, &OrientationModel::changed);

        m.rotateClockwise();          QCOMPARE(spy.count(), 1);
        m.toggleMirror();             QCOMPARE(spy.count(), 2);
        m.rotateCounterClockwise();   QCOMPARE(spy.count(), 3);
        m.reset();                    QCOMPARE(spy.count(), 4);

        // reset() on an already-identity model changes nothing, so it must
        // not re-notify: `changed` re-evaluates every orientation binding in
        // the QML scene and also pushes the value down the capture path.
        m.reset();                    QCOMPARE(spy.count(), 4);
    }

    // The lock is what stops the model promising the operator a rotation
    // that the recording in flight could never follow. Refused, not queued.
    void lockedRefusesEveryMutation() {
        OrientationModel m;
        m.rotateClockwise();
        m.toggleMirror();
        const int degreesBefore = m.degrees();
        const bool mirroredBefore = m.mirrored();

        m.setLocked(true);
        QSignalSpy spy(&m, &OrientationModel::changed);

        m.rotateClockwise();
        m.rotateCounterClockwise();
        m.toggleMirror();
        m.reset();

        QCOMPARE(m.degrees(), degreesBefore);
        QCOMPARE(m.mirrored(), mirroredBefore);
        QCOMPARE(spy.count(), 0);     // nothing moved, so nothing notified
    }

    void unlockingRestoresControl() {
        OrientationModel m;
        m.setLocked(true);
        m.rotateClockwise();
        QCOMPARE(m.degrees(), 0);     // refused while locked

        m.setLocked(false);
        m.rotateClockwise();
        QCOMPARE(m.degrees(), 90);    // and allowed again afterwards
    }

    void lockStateItselfNotifies() {
        OrientationModel m;
        QSignalSpy spy(&m, &OrientationModel::changed);
        m.setLocked(true);            QCOMPARE(spy.count(), 1);
        m.setLocked(true);            QCOMPARE(spy.count(), 1);   // idempotent
        m.setLocked(false);           QCOMPARE(spy.count(), 2);
    }

    // The value handed to the capture path must agree with what the
    // properties report, or the view and the saved files diverge.
    void exportedValueMatchesTheProperties() {
        OrientationModel m;
        m.rotateClockwise();
        m.toggleMirror();

        const Orientation v = m.value();
        QCOMPARE(v.degrees(), m.degrees());
        QCOMPARE(v.mirrored(), m.mirrored());
        QCOMPARE(m.transformedSize(QSizeF(640, 480)), QSizeF(480, 640));
    }
};

QTEST_MAIN(TestOrientationModel)
#include "test_orientation_model.moc"
