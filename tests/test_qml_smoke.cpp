#include <QtTest>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QMutex>
#include <QStringList>
#include <QKeySequence>

namespace {
QMutex g_qmlWarningsMutex;
QStringList g_qmlWarnings;

// A QML type-resolution failure, a missing QML_ELEMENT registration, or a
// mistyped binding (exactly Task 12's Deviation 2: VideoOutput.videoSink is
// read-only, and assigning to it is a runtime TypeError, not a compile
// error) surfaces only as qWarning()/qCritical() on stderr -- QML never
// throws a C++ exception for it, and a clean build and even a successful
// app start (rootObjects() non-empty) say nothing about it. This message
// handler is the only automated guard this project has against that whole
// failure class; previously it required a human reading stderr by hand.
void collectQmlWarnings(QtMsgType type, const QMessageLogContext&, const QString& msg) {
    if (type == QtWarningMsg || type == QtCriticalMsg) {
        QMutexLocker locker(&g_qmlWarningsMutex);
        g_qmlWarnings << msg;
    }
}
}

class TestQmlSmoke : public QObject {
    Q_OBJECT
private slots:
    void mainQmlLoadsWithNoWarnings() {
        qInstallMessageHandler(collectQmlWarnings);
        QQmlApplicationEngine engine;
        engine.loadFromModule("microscope", "Main");
        const bool loaded = !engine.rootObjects().isEmpty();
        qInstallMessageHandler(nullptr);

        QVERIFY2(loaded, "QQmlApplicationEngine failed to load Main.qml -- "
                          "see stderr for the underlying qrc:/ error");

        for (const QString& warning : g_qmlWarnings) {
            QVERIFY2(!warning.contains(QStringLiteral("qrc:/qt/qml/microscope")),
                     qPrintable(warning));
        }
    }

    // The keyboard path to snapshot (src/ui/Main.qml). A field operator
    // holds the scope in one hand and cannot always reach the on-screen
    // button, so this binding is the only other way to capture evidence.
    //
    // Existence alone is not the assertion: a Shortcut bound to the wrong
    // key, or one left auto-repeating, would both satisfy a mere
    // findChild() check while being useless or actively harmful. Both
    // properties are therefore pinned explicitly, so deleting the
    // Shortcut, rebinding it, or dropping autoRepeat all fail this test.
    void snapshotShortcutIsSpaceAndDoesNotAutoRepeat() {
        QQmlApplicationEngine engine;
        engine.loadFromModule("microscope", "Main");
        QVERIFY2(!engine.rootObjects().isEmpty(),
                 "Main.qml failed to load -- see stderr");

        QObject* shortcut =
            engine.rootObjects().first()->findChild<QObject*>(
                QStringLiteral("snapshotShortcut"));
        QVERIFY2(shortcut,
                 "Main.qml has no object named snapshotShortcut -- the "
                 "keyboard path to snapshot is gone");

        QCOMPARE(shortcut->property("sequence").value<QKeySequence>(),
                 QKeySequence(Qt::Key_Space));

        // Shortcut.autoRepeat defaults to TRUE in Qt. Left on, holding the
        // key fires repeatedly and writes one JPEG per repeat -- filling
        // the output folder with near-identical frames and burning the
        // disk-space budget DiskPolicy exists to protect.
        QCOMPARE(shortcut->property("autoRepeat").toBool(), false);
    }
};

QTEST_MAIN(TestQmlSmoke)
#include "test_qml_smoke.moc"
