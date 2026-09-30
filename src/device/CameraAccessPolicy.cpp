#include "device/CameraAccessPolicy.h"

DeviceAbsenceReason CameraAccessPolicy::classify(bool sawDeviceAtStartup, int currentDeviceCount) {
    if (currentDeviceCount == 0 && sawDeviceAtStartup) return DeviceAbsenceReason::LikelyAccessDenied;
    return DeviceAbsenceReason::NoScopeEverSeen;
}
