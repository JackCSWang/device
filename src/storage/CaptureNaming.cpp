#include "storage/CaptureNaming.h"

QString CaptureNaming::nextName(const QDateTime& when,
                                const QString& extension,
                                const ExistsFn& exists) {
    const QString stamp = when.toString(QStringLiteral("yyyyMMdd_HHmmss"));
    const QString base  = QStringLiteral("%1_%2").arg(QLatin1String(Prefix), stamp);

    QString candidate = QStringLiteral("%1.%2").arg(base, extension);
    int n = 1;
    while (exists(candidate)) {
        ++n;
        candidate = QStringLiteral("%1_%2.%3").arg(base).arg(n).arg(extension);
    }
    return candidate;
}
