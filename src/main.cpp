#include <QGuiApplication>
#include <QQmlApplicationEngine>

int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("Microscope"));

    QQmlApplicationEngine engine;
    engine.loadFromModule("microscope", "Main");
    if (engine.rootObjects().isEmpty()) return -1;
    return app.exec();
}
