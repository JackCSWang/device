#include "device/DeviceRegistry.h"
#include <QMediaDevices>

DeviceRegistry::DeviceRegistry(QObject* parent) : QObject(parent) {}

QList<ScopeDevice> DeviceRegistry::available() const {
    QList<ScopeDevice> out;
    for (const QCameraDevice& d : QMediaDevices::videoInputs()) {
        if (d.isNull()) continue;
        out.append(ScopeDevice{QString::fromUtf8(d.id()), d.description(), d});
    }
    return out;
}

void DeviceRegistry::watch() {
    auto* devices = new QMediaDevices(this);
    connect(devices, &QMediaDevices::videoInputsChanged, this, &DeviceRegistry::refresh);
    refresh();
}

void DeviceRegistry::refresh() {
    const QList<ScopeDevice> now = available();

    QList<QString> currentIds;
    for (const ScopeDevice& d : now) {
        currentIds.append(d.id);
        if (!m_knownIds.contains(d.id)) emit attached(d);
    }
    for (const QString& id : m_knownIds)
        if (!currentIds.contains(id)) emit detached(id);

    m_knownIds = currentIds;
}
