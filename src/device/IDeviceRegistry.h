#pragma once
#include <QCameraDevice>
#include <QList>
#include <QMetaType>
#include <QObject>
#include <QString>

struct ScopeDevice {
    QString id;
    QString description;
    QCameraDevice device;
};

// Needed so a QSignalSpy in a test can round-trip this through QVariant, and
// so attached() survives a queued connection. Without it a spy on
// IDeviceRegistry::attached records an invalid QVariant and every assertion
// about *which* device attached passes while proving nothing -- the same
// trap StopReason's own Q_DECLARE_METATYPE exists to avoid.
Q_DECLARE_METATYPE(ScopeDevice)

// The device-enumeration seam. `DeviceRegistry` implements it with
// QMediaDevices; tests implement it with a scriptable fake.
//
// This seam exists because the whole open/teardown/reopen lifecycle -- the
// single most failure-prone part of this app, and the home of three of the
// four Criticals found in final review -- was previously reachable only
// through AppContext, in the app target, where by this project's own
// structural rule no test can go. CaptureSession owns that lifecycle now,
// and this interface is what lets a test drive it: attach, detach, reseat,
// and a device that stays enumerated while refusing to stream.
class IDeviceRegistry : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    ~IDeviceRegistry() override = default;

    virtual QList<ScopeDevice> available() const = 0;

    // Begins watching for attach/detach. Implementations emit attached()
    // synchronously for every device already present, so a caller that also
    // polls available() itself must be idempotent.
    virtual void watch() = 0;

signals:
    void attached(ScopeDevice device);
    void detached(QString id);
};
