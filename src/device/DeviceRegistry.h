#pragma once
#include "device/IDeviceRegistry.h"

// Enumerates capture devices and reports attach/detach. On desktop the OS
// class driver has already bound the UVC device, so this is a listing, not
// a claim -- which is why no elevated privileges are needed (spec 4.1).
class DeviceRegistry : public IDeviceRegistry {
    Q_OBJECT
public:
    explicit DeviceRegistry(QObject* parent = nullptr);
    QList<ScopeDevice> available() const override;
    void watch() override;

private:
    void refresh();
    QList<QString> m_knownIds;
};
