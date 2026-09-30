#pragma once
#include "device/IDeviceRegistry.h"
#include <QString>

// Spec 8.5 step 1, in full: "device enumerates; auto-open if exactly one
// scope, **otherwise prompt**".
//
// Only the first half was ever implemented. The code bound
// `devices.first()`, which on the spec's own field hardware -- a technician
// with a laptop (§1) -- is the integrated webcam, not the microscope. The
// execution ledger records that happening for real: Task 10's hardware
// tests bound to "Integrated Camera". On a two-camera machine that makes
// every other guarantee in this app vacuous, because unplugging the scope
// mid-recording does nothing: the scope was never the source.
//
// Deliberately not a heuristic on the description string. The scope on the
// development machine enumerates as "HD camera" and the laptop's built-in
// one as "Integrated Camera" -- any keyword rule that looked for "scope",
// "micro", or "USB" would pick the webcam on the very device that matters.
// So: one input, open it; several, ask; and remember by id what was chosen.
struct DeviceChoice {
    enum class Kind {
        // Nothing enumerated at all.
        NoDevice,
        // Exactly one, or a remembered one that is present: open it.
        Open,
        // Several, none of them remembered: the technician must choose.
        Prompt,
    };
    Kind kind = Kind::NoDevice;
    QString deviceId;   // meaningful only for Kind::Open
};

class DeviceSelection {
public:
    // rememberedId is the id last chosen (or last successfully opened); it
    // may be empty, and may name a device that is no longer present, both
    // of which are ordinary.
    static DeviceChoice choose(const QList<ScopeDevice>& devices,
                               const QString& rememberedId);
};
