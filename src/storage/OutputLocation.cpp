#include "storage/OutputLocation.h"
#include <QDir>
#include <QStandardPaths>

QString OutputLocation::defaultDirectory() {
    QString base = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
    if (base.isEmpty())
        base = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    if (base.isEmpty())
        base = QDir::homePath();
    return QDir(base).filePath(QStringLiteral("Microscope"));
}

bool OutputLocation::ensureExists(const QString& dir) {
    return QDir().mkpath(dir);
}
