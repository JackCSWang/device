#include <QtTest>
#include <QSet>
#include "storage/CaptureNaming.h"
#include "storage/OutputLocation.h"

class TestCaptureNaming : public QObject {
    Q_OBJECT
    static QDateTime when() {
        return QDateTime(QDate(2026, 9, 30), QTime(14, 5, 9));
    }
private slots:
    void buildsTimestampedName() {
        auto none = [](const QString&) { return false; };
        QCOMPARE(CaptureNaming::nextName(when(), "jpg", none),
                 QStringLiteral("scope_20260930_140509.jpg"));
    }

    void respectsExtension() {
        auto none = [](const QString&) { return false; };
        QCOMPARE(CaptureNaming::nextName(when(), "mp4", none),
                 QStringLiteral("scope_20260930_140509.mp4"));
    }

    // Review Focus 2: two taps in the same second must not overwrite.
    void suffixesOnCollision() {
        QSet<QString> taken{QStringLiteral("scope_20260930_140509.jpg")};
        auto exists = [&](const QString& n) { return taken.contains(n); };
        const QString second = CaptureNaming::nextName(when(), "jpg", exists);
        QCOMPARE(second, QStringLiteral("scope_20260930_140509_2.jpg"));
        QVERIFY(!taken.contains(second));
    }

    void suffixesRepeatedlyUntilFree() {
        QSet<QString> taken{
            QStringLiteral("scope_20260930_140509.jpg"),
            QStringLiteral("scope_20260930_140509_2.jpg"),
            QStringLiteral("scope_20260930_140509_3.jpg")};
        auto exists = [&](const QString& n) { return taken.contains(n); };
        QCOMPARE(CaptureNaming::nextName(when(), "jpg", exists),
                 QStringLiteral("scope_20260930_140509_4.jpg"));
    }

    void neverReturnsATakenName() {
        QSet<QString> taken;
        auto exists = [&](const QString& n) { return taken.contains(n); };
        for (int i = 0; i < 50; ++i) {
            const QString n = CaptureNaming::nextName(when(), "jpg", exists);
            QVERIFY2(!taken.contains(n), qPrintable(n));
            taken.insert(n);
        }
        QCOMPARE(taken.size(), 50);
    }

    void defaultDirectoryIsUsableAndWritable() {
        const QString dir = OutputLocation::defaultDirectory();
        QVERIFY(!dir.isEmpty());
        QVERIFY(OutputLocation::ensureExists(dir));
        QVERIFY(QFileInfo(dir).isWritable());
    }
};

QTEST_MAIN(TestCaptureNaming)
#include "test_capture_naming.moc"
