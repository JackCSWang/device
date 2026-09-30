#pragma once
#include <QCameraDevice>
#include <QList>
#include <QObject>
#include <QString>

struct ScopeDevice {
    QString id;
    QString description;
    QCameraDevice device;
};

// Enumerates capture devices and reports attach/detach. On desktop the OS
// class driver has already bound the UVC device, so this is a listing, not
// a claim -- which is why no elevated privileges are needed (spec 4.1).
class DeviceRegistry : public QObject {
    Q_OBJECT
public:
    explicit DeviceRegistry(QObject* parent = nullptr);
    QList<ScopeDevice> available() const;
    void watch();

signals:
    void attached(ScopeDevice device);
    void detached(QString id);

private:
    void refresh();
    QList<QString> m_knownIds;
};
