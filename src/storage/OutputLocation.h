#pragma once
#include <QString>

// Where captures land: a plain, user-visible folder. No database, no index.
class OutputLocation {
public:
    static QString defaultDirectory();
    static bool ensureExists(const QString& dir);
};
