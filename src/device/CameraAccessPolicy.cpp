#include "device/CameraAccessPolicy.h"

DeviceAbsenceReason CameraAccessPolicy::classify(bool sawDeviceAtStartup,
                                                 int currentDeviceCount,
                                                 bool everOpenedADevice) {
    if (currentDeviceCount != 0) return DeviceAbsenceReason::NoScopeEverSeen;
    // A pipeline that ever ran proves this app was granted access, so an
    // empty list now is an unplugged cable, not a denial.
    if (everOpenedADevice) return DeviceAbsenceReason::NoScopeEverSeen;
    if (sawDeviceAtStartup) return DeviceAbsenceReason::LikelyAccessDenied;
    return DeviceAbsenceReason::NoScopeEverSeen;
}

bool CameraAccessPolicy::activationFailureMayBeAccessDenied(
    bool deviceStillEnumerated, bool deliveredAFrameThisPipeline) {
    return deviceStillEnumerated && !deliveredAFrameThisPipeline;
}
