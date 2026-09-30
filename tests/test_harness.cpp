#include <QtTest>

class TestHarness : public QObject {
    Q_OBJECT
private slots:
    void qtTestItselfWorks() { QCOMPARE(1 + 1, 2); }
};

QTEST_MAIN(TestHarness)
#include "test_harness.moc"
