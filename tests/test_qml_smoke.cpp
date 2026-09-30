#include <QtTest>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QMutex>
#include <QStringList>

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
};

QTEST_MAIN(TestQmlSmoke)
#include "test_qml_smoke.moc"
