#include "device/DeviceSelection.h"

DeviceChoice DeviceSelection::choose(const QList<ScopeDevice>& devices,
                                     const QString& rememberedId) {
    if (devices.isEmpty()) return {};

    // A remembered device that is present wins outright, at any list size.
    // That is what makes the prompt a one-time cost per machine rather than
    // something a technician faces on every launch.
    if (!rememberedId.isEmpty()) {
        for (const ScopeDevice& d : devices)
            if (d.id == rememberedId)
                return {DeviceChoice::Kind::Open, d.id};
    }

    if (devices.size() == 1)
        return {DeviceChoice::Kind::Open, devices.first().id};

    // Several, and no idea which. Opening one anyway is exactly the defect
    // this replaces: it silently picks the webcam and then reports success.
    return {DeviceChoice::Kind::Prompt, QString()};
}
