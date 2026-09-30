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
class ICaptureSource : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    ~ICaptureSource() override = default;

    // Opens the device and begins streaming.
    //
    // WHAT THE RETURN VALUE MEANS: only that the *synchronous* part of
    // opening did not fail outright. It is NOT evidence that the device
    // activated, that permission was granted, or that a frame will ever
    // arrive -- spec 10.2 is explicit that "a successful open() is not
    // evidence that streaming works".
    //
    // WHAT IT DOES NOT MEAN, concretely: QtCaptureSource returns
    // QCamera::isAvailable() immediately after QCamera::start(), and camera
    // activation is asynchronous on every backend. Permission denials, busy
    // devices, isochronous-bandwidth starvation and backend failures all
    // arrive *later*, as stopped(StopReason::Error, ...). So a false return
    // is effectively unreachable in practice, and any diagnosis that only
    // runs on `!start()` is dead code. Diagnose on the Error path instead
    // (see CaptureController::onSourceStopped).
    virtual bool start() = 0;

    // Stops streaming and releases the device.
    //
    // MUST emit stopped(StopReason::Requested, ...) SYNCHRONOUSLY, before
    // returning. CaptureController's stall handler depends on it: it sets
    // m_stallStopping, calls stop(), and relies on onSourceStopped running
    // re-entrantly inside that call so a recording cut short by a stall is
    // reported as interrupted rather than silently ending. If an
    // implementation deferred the emission, that flag would be consumed by
    // some later, unrelated stop instead.
    //
    // Must be safe to call when not running (a no-op, emitting nothing).
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

    // The OS's own path to the device node, where the platform has one. On
    // Linux (V4L2) this is the `/dev/videoN` path, which is what spec
    // 10.3's `EACCES` row needs: the readability check used to hardcode
    // `/dev/video0`, so on any machine where the scope enumerates as
    // /dev/video1 or higher -- entirely ordinary with a built-in webcam
    // present -- a real permission problem went undetected and the
    // technician got the generic "could not open the scope" instead of the
    // actionable `usermod -aG video` fix.
    //
    // Empty for implementations with no such concept (the fake) and on
    // platforms where the id is an opaque handle rather than a path
    // (Windows, macOS), where the caller must skip the check.
    virtual QString deviceNode() const { return {}; }

signals:
    // Delivers a borrowed QVideoFrame: the underlying buffer belongs to the
    // source and is recycled. Any consumer keeping it past the slot MUST
    // call Frame::deepCopy first (spec 8.3).
    //
    // THREADING CONTRACT: always emitted on the thread the source lives on
    // -- the main thread in this app. Consumers may therefore connect with
    // the default AutoConnection and treat delivery as direct.
    //
    // Implementations that receive frames from a backend thread must
    // enforce this themselves, and must do it with latest-frame-wins
    // coalescing rather than one queued event per frame: a queued signal
    // per frame is the unbounded queue spec 8.4 explicitly forbids, and
    // every event in it would hold a reference to a buffer the backend is
    // busy recycling. FrameRelay exists for exactly that, and
    // QtCaptureSource routes through it.
    //
    // Measured on this project's reference machine (Windows 10, Qt 6.8.3
    // MinGW), both the `windows` (Media Foundation) and `ffmpeg` backends
    // deliver QVideoSink::videoFrameChanged on the sink's own thread, i.e.
    // the main thread, so no coalescing is active there. macOS and Linux
    // have never been run; the relay is what makes the contract hold
    // regardless of what they do.
    void frameReady(const QVideoFrame& frame, qint64 timestampUs);

    // Exactly one per successful start(), eventually. Requested is emitted
    // synchronously from stop(); Detached and Error arrive whenever the
    // device or backend says so.
    void stopped(StopReason reason, QString detail);
};
