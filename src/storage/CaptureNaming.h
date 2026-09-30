#pragma once
#include <QDateTime>
#include <QString>
#include <functional>

// Timestamped capture filenames. Collision-free by construction: a
// technician tapping the shutter twice in one second must not overwrite
// the first image, because the overwrite would be silent.
class CaptureNaming {
public:
    using ExistsFn = std::function<bool(const QString&)>;
    static constexpr const char* Prefix = "scope";

    static QString nextName(const QDateTime& when,
                            const QString& extension,
                            const ExistsFn& exists);
};
