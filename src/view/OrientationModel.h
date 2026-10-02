#pragma once
#include "view/Orientation.h"
#include <QObject>
#include <QSizeF>
#include <qqmlintegration.h>

// QML-facing wrapper for Orientation. Every semantic -- the 90-degree
// stepping, the mirror-before-rotate order, the axis swap -- stays in
// Orientation where it is unit-tested; this adds only property
// notification, the QML type, and the recording lock.
class OrientationModel : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(int degrees READ degrees NOTIFY changed)
    Q_PROPERTY(bool mirrored READ mirrored NOTIFY changed)
    Q_PROPERTY(bool identity READ isIdentity NOTIFY changed)
    // True while a recording is in flight. IRecorder::start() fixes the
    // frame size for the whole take, so a quarter turn mid-recording would
    // feed the encoder frames of the wrong size. The controls bind to this
    // to grey themselves out, and the invokables below refuse outright, so
    // the model can never drift out of step with what the recording is
    // actually doing. CaptureController additionally latches its own copy
    // at record start -- that latch, not this flag, is what makes the
    // corruption unreachable.
    Q_PROPERTY(bool locked READ locked WRITE setLocked NOTIFY changed)

public:
    using QObject::QObject;

    int degrees() const { return m_o.degrees(); }
    bool mirrored() const { return m_o.mirrored(); }
    bool isIdentity() const { return m_o.isIdentity(); }
    bool locked() const { return m_locked; }
    void setLocked(bool locked);

    // The plain value, for handing to the capture path.
    Orientation value() const { return m_o; }
    QSizeF transformedSize(QSizeF source) const { return m_o.transformedSize(source); }

    Q_INVOKABLE void rotateClockwise();
    Q_INVOKABLE void rotateCounterClockwise();
    Q_INVOKABLE void toggleMirror();
    Q_INVOKABLE void reset();

signals:
    void changed();

private:
    Orientation m_o;
    bool m_locked = false;
};
