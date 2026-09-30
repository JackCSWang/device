#pragma once
#include <QObject>
#include <QSize>
#include <QStringList>
#include <QVideoFrame>

enum class StopReason { Requested, Detached, Error };

// Required so QSignalSpy can round-trip the enum through QVariant. Without
// it, spy.at(0).at(0).value<StopReason>() silently returns Requested for
// every reason and the detach tests pass while proving nothing.
Q_DECLARE_METATYPE(StopReason)

// The one seam in the system. QtCaptureSource implements it with QCamera;
// FakeCaptureSource implements it with a synthetic pattern and failure
// injection. Nothing downstream knows which it has, which is what lets the
// whole failure table be tested with no scope attached.
//
// frameReady delivers a borrowed QVideoFrame: the underlying buffer belongs
// to the source and is recycled. Any consumer keeping it past the slot MUST
// call Frame::deepCopy first (spec 8.3).
class ICaptureSource : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    ~ICaptureSource() override = default;

    virtual bool start() = 0;
    virtual void stop() = 0;
    virtual QSize frameSize() const = 0;

    // Advances to the next format in the preference chain, for the watchdog's
    // downgrade-and-retry (spec 10.2). Sources with nothing to fall back to
    // return false, which is why this is virtual rather than pure.
    virtual bool selectNextFormat() { return false; }

    // Human-readable list of what the device advertised, for the "all formats
    // failed" message. Reporting the list is what makes a silent scope
    // diagnosable from the field (spec 10.3).
    virtual QStringList formatDescriptions() const { return {}; }

signals:
    void frameReady(const QVideoFrame& frame, qint64 timestampUs);
    void stopped(StopReason reason, QString detail);
};
