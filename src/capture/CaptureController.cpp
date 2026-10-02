#include "capture/CaptureController.h"
#include "output/IRecorder.h"
#include "output/SnapshotWriter.h"
#include "storage/CaptureNaming.h"
#include "storage/DiskPolicy.h"
#include "device/StopClassification.h"
#include "storage/OutputLocation.h"
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include "core/Frame.h"
#include <QVideoSink>

namespace {
// Re-renders one frame with the operator's orientation baked into its
// pixels, for the recorder. Only ever called when the orientation is not
// identity, so the ordinary un-rotated recording path is untouched and
// costs nothing.
//
// Qt 6.8 also offers QVideoFrame::setRotation()/setMirrored(), which would
// be metadata-only and therefore free -- but whether this build's FFmpeg
// recorder writes that through to the MP4 display matrix is unverified, and
// metadata that gets silently dropped would produce evidence oriented
// differently from what the operator saw. A pixel permutation is certain,
// and at this sensor's 640x480 it is a rounding error against the H.264
// encode already happening on every frame.
QVideoFrame orientedFrame(const QVideoFrame& src, const Orientation& o,
                          qint64 timestampUs) {
    const Frame owned = Frame::deepCopy(src, timestampUs);
    if (!owned.isValid())
        return src;   // unreadable buffer: feed the original rather than drop it
    return QVideoFrame(o.apply(owned.image()));
}
}

CaptureController::CaptureController(ICaptureSource* source, IRecorder* recorder,
                                     SnapshotWriter* writer, QString outputDir,
                                     QObject* parent)
    : QObject(parent), m_source(source), m_recorder(recorder), m_writer(writer),
      m_displaySink(new QVideoSink(this)), m_outputDir(std::move(outputDir)) {

    connect(m_source, &ICaptureSource::frameReady, this, &CaptureController::onFrame);
    connect(m_source, &ICaptureSource::stopped, this, &CaptureController::onSourceStopped);

    connect(m_writer, &SnapshotWriter::written, this,
            [this](const QString& path, QSize) {
                emit snapshotSaved(path);
                emit status(tr("Snapshot saved as %1.").arg(QFileInfo(path).fileName()));
            });
    connect(m_writer, &SnapshotWriter::failed, this,
            [this](const QString&, const QString& why) { emit status(why); });

    connect(m_recorder, &IRecorder::finished, this,
            [this](const QString& path, qint64) {
                emit recordingSaved(path);
                // The outcome is now known, so this is the first point at
                // which a detach-, error-, or stall-interrupted recording
                // can be reported without asserting something not yet true.
                // Exactly one status message reaches the user either way.
                if (m_pendingInterruption == RecordingInterruption::Detach ||
                    m_pendingInterruption == RecordingInterruption::Error) {
                    const bool wasDetach = m_pendingInterruption == RecordingInterruption::Detach;
                    const QString cause = m_pendingInterruptionDetail;
                    m_pendingInterruption = RecordingInterruption::None;
                    m_pendingInterruptionDetail.clear();
                    const QString what = wasDetach ? tr("The scope was disconnected.")
                                                    : tr("The scope stopped.");
                    QString message = tr("%1 The recording was saved as %2. Reconnect the "
                                         "scope to continue.").arg(what, QFileInfo(path).fileName());
                    if (!cause.isEmpty())
                        message += tr(" Reported cause: %1.").arg(cause);
                    // sourceLost may destroy this controller (a direct,
                    // same-thread connection can delete it synchronously,
                    // and it can even destroy the QtRecorder whose own
                    // finished() emission is still unwinding beneath this
                    // lambda) -- so it must be the very last thing this
                    // path touches `this` for. Nothing may follow it.
                    emit status(message);
                    emit sourceLost(message, wasDetach ? StopReason::Detached : StopReason::Error);
                } else if (m_pendingInterruption == RecordingInterruption::Stall) {
                    m_pendingInterruption = RecordingInterruption::None;
                    emit status(tr("The video stream stopped, so the recording was ended and "
                                   "saved as %1. Recording will not resume automatically.")
                                    .arg(QFileInfo(path).fileName()));
                } else if (m_pendingInterruption == RecordingInterruption::DiskFull) {
                    m_pendingInterruption = RecordingInterruption::None;
                    emit status(tr("Storage is nearly full, so the recording was ended and "
                                   "saved as %1. Free some space before recording again.")
                                    .arg(QFileInfo(path).fileName()));
                } else {
                    emit status(tr("Recording saved as %1.").arg(QFileInfo(path).fileName()));
                }
            });
    connect(m_recorder, &IRecorder::failed, this,
            [this](const QString&, const QString& why) {
                if (m_pendingInterruption == RecordingInterruption::Detach ||
                    m_pendingInterruption == RecordingInterruption::Error) {
                    const bool wasDetach = m_pendingInterruption == RecordingInterruption::Detach;
                    const QString cause = m_pendingInterruptionDetail;
                    m_pendingInterruption = RecordingInterruption::None;
                    m_pendingInterruptionDetail.clear();
                    const QString what = wasDetach ? tr("The scope was disconnected.")
                                                    : tr("The scope stopped.");
                    QString message =
                        tr("%1 The recording could not be saved: %2").arg(what, why);
                    if (!cause.isEmpty())
                        message += tr(" Reported cause: %1.").arg(cause);
                    // See the matching comment in the finished() handler
                    // above: sourceLost must be last.
                    emit status(message);
                    emit sourceLost(message, wasDetach ? StopReason::Detached : StopReason::Error);
                } else if (m_pendingInterruption == RecordingInterruption::Stall) {
                    m_pendingInterruption = RecordingInterruption::None;
                    emit status(tr("The video stream stopped, so the recording was ended. It "
                                   "could not be saved: %1").arg(why));
                } else if (m_pendingInterruption == RecordingInterruption::DiskFull) {
                    m_pendingInterruption = RecordingInterruption::None;
                    emit status(tr("Storage is nearly full, so the recording was ended. It "
                                   "could not be saved: %1 Free some space before "
                                   "recording again.").arg(why));
                } else {
                    emit status(why);
                }
            });

    m_firstFrameTimer.setSingleShot(true);
    m_firstFrameTimer.setInterval(FirstFrameTimeoutMs);
    connect(&m_firstFrameTimer, &QTimer::timeout, this, [this] {
        // open() succeeded but nothing streamed -- the classic shared-hub
        // isochronous bandwidth failure (spec 10.2). Walk down the format
        // preference chain before giving up; complaining without retrying is
        // not what the spec asks for.
        if (m_source->selectNextFormat()) {
            emit status(tr("No video at this resolution. Trying a lower one."));
            // m_source->stop() emits stopped(Requested, ...) synchronously,
            // re-entering onSourceStopped below. That handler stops both
            // watchdogs and clears m_snapshotArmed,
            // but does NOT touch m_stallStopping (only the stall-watchdog
            // path sets that) and only sets m_pendingInterruption when a
            // recording is in flight, which it cannot be here: no frame has
            // ever arrived, so nothing has had a chance to start one through
            // the normal UI flow. The re-entrant call also is not
            // StopReason::Detached, so it never reaches sourceLost. So this
            // restart cannot trip the stall or interruption bookkeeping.
            // What it DOES do is stop m_firstFrameTimer (it's a no-op; the
            // timer already fired and is a single-shot). Restarting the
            // timer must therefore happen last, after stop()/start(), so
            // that reentrant stop() is not able to disarm the *new* watchdog
            // out from under this retry.
            m_source->stop();
            m_sawFirstFrame = false;
            m_source->start();
            m_firstFrameTimer.start();
            return;
        }

        const QStringList advertised = m_source->formatDescriptions();
        emit firstFrameTimedOut();
        emit formatsExhausted(advertised);
        emit status(tr("No video received from the scope at any resolution. "
                       "Nothing was saved. Try a direct USB port instead of a hub. "
                       "The scope offered: %1")
                        .arg(advertised.isEmpty() ? tr("no formats")
                                                  : advertised.join(QStringLiteral(", "))));
    });

    m_stallTimer.setSingleShot(true);
    m_stallTimer.setInterval(StallTimeoutMs);
    connect(&m_stallTimer, &QTimer::timeout, this, [this] {
        emit status(tr("The video stream stopped. Reconnecting."));
        // m_source->stop() can re-enter onSourceStopped synchronously (it
        // does for FakeCaptureSource); this flag is how that handler tells
        // a stall-initiated stop apart from any other StopReason::Requested,
        // so a recording in flight gets reported as interrupted rather than
        // silently ending (spec 10.1's guarantee is not detach-only).
        m_stallStopping = true;
        m_source->stop();
        m_sawFirstFrame = false;
        m_source->start();
        m_firstFrameTimer.start();
    });
}

void CaptureController::setFreeSpaceProbe(std::function<qint64(const QString&)> probe) {
    m_diskSpace.setProbe(std::move(probe));
}

bool CaptureController::isRecording() const {
    return m_recorder && m_recorder->isRecording();
}

void CaptureController::begin() {
    OutputLocation::ensureExists(m_outputDir);
    m_sawFirstFrame = false;
    if (!m_source->start()) {
        // Reachable only for a source whose *synchronous* open fails --
        // which, for QtCaptureSource, effectively never happens: it
        // returns QCamera::isAvailable() immediately after start(), and
        // activation is asynchronous, so permission denials, busy devices
        // and backend failures all arrive later via stopped(Error). The
        // Linux EACCES diagnosis and the "could not open" wording used to
        // live here together, which made both of them dead code. The
        // diagnosis moved to onSourceStopped's Error path; this stays as
        // the honest last resort for a source that really does fail
        // up front (see ICaptureSource::start()'s contract).
        emit status(tr("Could not open the scope. Nothing was saved. "
                       "Check the cable, then reconnect the device."));
        return;
    }
    m_firstFrameTimer.start();
}

QString CaptureController::reserveName(const QString& extension) const {
    const QDir dir(m_outputDir);
    // dir.exists() alone isn't enough: the file for a name handed out a
    // moment ago may not have reached disk yet (SnapshotWriter's encode
    // runs on a thread pool), so also check the names this process has
    // already reserved in this run.
    const QString name = CaptureNaming::nextName(
        QDateTime::currentDateTime(), extension,
        [&dir, this](const QString& candidate) {
            return dir.exists(candidate) || m_reservedNames.contains(candidate);
        });
    m_reservedNames.insert(name);
    return dir.filePath(name);
}

void CaptureController::onFrame(const QVideoFrame& frame, qint64 timestampUs) {
    if (!m_sawFirstFrame) {
        m_sawFirstFrame = true;
        m_firstFrameTimer.stop();
    }
    m_stallTimer.start();

    m_displaySink->setVideoFrame(frame);          // display: latest wins, drops freely

    if (m_snapshotArmed) {
        m_snapshotArmed = false;
        m_writer->write(frame, timestampUs, reserveName(QStringLiteral("jpg")),
                        m_orientation);
    }

    if (m_recorder->isRecording()) {
        // Important 8: the throttled reading, not a fresh stat. This runs
        // once per recorded frame -- ~30/s on the display thread -- and
        // spec 8.2 says the capture path never blocks on disk.
        if (DiskPolicy::mustStopRecording(m_diskSpace.freeBytesFor(m_outputDir))) {
            // Set before finalizeAndStop(), which can drive the recorder
            // all the way to finished()/failed() synchronously -- that
            // handler is where the outcome, and so the filename, becomes
            // sayable. Nothing is announced here, so the technician gets
            // exactly one message and it names the file and the action.
            m_pendingInterruption = RecordingInterruption::DiskFull;
            m_recorder->finalizeAndStop();
        } else {
            // m_recordingOrientation, NOT m_orientation: the encoder was
            // opened at the latched orientation's frame size and every
            // frame of this take must match it.
            m_recorder->feed(m_recordingOrientation.isIdentity()
                                 ? frame
                                 : orientedFrame(frame, m_recordingOrientation,
                                                 timestampUs),
                             timestampUs);
        }
    }
}

void CaptureController::takeSnapshot() {
    if (!m_sawFirstFrame) {
        emit status(tr("No video yet, so nothing was saved. "
                       "Wait for the live view, then try again."));
        return;
    }
    if (!DiskPolicy::canSnapshot(DiskPolicy::freeBytesFor(m_outputDir))) {
        emit status(tr("Not enough free storage for a snapshot. Nothing was saved. "
                       "Free some space and try again."));
        return;
    }
    m_snapshotArmed = true;   // the next frame is the one captured
}

void CaptureController::setOrientation(const Orientation& orientation) {
    m_orientation = orientation;
}

void CaptureController::startRecording() {
    if (isRecording()) return;
    // Must-fix minor 9: mirror takeSnapshot()'s own guard. Without it,
    // pressing Record before the first frame started a recorder against a
    // fabricated 1920x1080 resolution for what might be a 640x480 sensor
    // (the scope on the development machine is exactly that), and produced
    // a take that could not contain anything.
    if (!m_sawFirstFrame) {
        emit status(tr("No video yet, so nothing was recorded. "
                       "Wait for the live view, then press Record."));
        return;
    }
    if (!DiskPolicy::canStartRecording(DiskPolicy::freeBytesFor(m_outputDir))) {
        emit status(tr("Not enough free storage to record. Nothing was saved. "
                       "Free at least 500 MB and try again."));
        return;
    }
    // m_sawFirstFrame means a real frame has been through onFrame, so the
    // source has a real size to report -- no fabricated fallback. A source
    // that still reports nothing is broken, and saying so beats recording
    // at a resolution nobody chose.
    const QSize rawSize = m_source->frameSize();
    if (rawSize.isEmpty()) {
        emit status(tr("The scope did not report a video size, so nothing was "
                       "recorded. Reconnect the scope, then try again."));
        return;
    }
    // Latch the orientation for the whole take, and open the encoder at the
    // size those frames will actually have. A quarter turn swaps the axes:
    // a 640x480 sensor recorded at 90 degrees is a 480x640 file.
    m_recordingOrientation = m_orientation;
    const QSize size = m_recordingOrientation.transformedSize(rawSize);

    const QString path = reserveName(QStringLiteral("mp4"));
    // A detach, error, or stall from a *previous* recording may still be
    // waiting on an outcome here (the finished()/failed() handler that would
    // normally clear m_pendingInterruption hasn't run yet). This fresh
    // recording must not inherit that flag or its stale detail: without this
    // reset, this recording's own, unrelated finished() would later be
    // reported as "the scope was disconnected. The recording was saved as
    // <this file>" -- the original false claim, just relocated onto a
    // healthy take.
    //
    // Must-fix minor 2: the reset happens BEFORE start(), not after.
    // start() calls QMediaRecorder::record(), which can reach
    // errorOccurred -- and so failed(), and so the interruption-reporting
    // handler -- synchronously, beneath this call. A reset afterwards would
    // clear the flag only once that handler had already read the stale one.
    m_pendingInterruption = RecordingInterruption::None;
    m_pendingInterruptionDetail.clear();
    if (!m_recorder->start(path, size, 30.0)) {
        emit status(tr("Could not start recording. Nothing was saved."));
        return;
    }
    emit status(tr("Recording to %1.").arg(QFileInfo(path).fileName()));
}

void CaptureController::stopRecording() {
    if (!isRecording()) return;
    m_recorder->finalizeAndStop();
}

void CaptureController::onSourceStopped(StopReason reason, const QString& detail) {
    m_firstFrameTimer.stop();
    m_stallTimer.stop();
    // Every member write this function needs happens up front, before
    // anything that could destroy `this` -- see the two `return`s below.
    m_snapshotArmed = false;

    const bool wasRecording = isRecording();
    const bool stallInitiated = m_stallStopping;
    m_stallStopping = false;

    // Finalize before reporting anything. A truncated MP4 has no moov atom
    // and will not open, so the evidence is simply gone (spec 10.1). The
    // outcome (finished/failed) is asynchronous -- IRecorder never
    // guarantees otherwise -- so whether it succeeded is not yet known here.
    // Record *why* the recording was cut short and let the finished()/
    // failed() handler report the outcome once it actually is known.
    if (wasRecording) {
        if (reason == StopReason::Detached || reason == StopReason::Error) {
            m_pendingInterruption = (reason == StopReason::Detached)
                ? RecordingInterruption::Detach : RecordingInterruption::Error;
            // Carried to the deferred finished()/failed() handler, which is
            // the only place left that can still name the driver's cause
            // once this function returns (spec 10.3; task-15-report.md
            // Important 2 -- previously discarded entirely for this path).
            m_pendingInterruptionDetail = detail;
        } else if (stallInitiated) {
            m_pendingInterruption = RecordingInterruption::Stall;
        }
        // finalizeAndStop() can synchronously drive the recorder all the way
        // to finished()/failed(), which -- since m_pendingInterruption is
        // now set -- can itself emit sourceLost() and let a consumer delete
        // this controller before this call even returns. `this` must not be
        // touched again on this path; the destructor already ran by the
        // time control gets back here if that happened.
        m_recorder->finalizeAndStop();
        return;
    }

    if (reason == StopReason::Detached || reason == StopReason::Error) {
        // "disconnected" sends a technician to check a cable that is fine
        // whenever the real cause is a driver/backend error with the scope
        // still plugged in (a busy device, a bandwidth failure) -- Task 10
        // correctly split Error out from Detached for exactly this reason,
        // but nothing spoke for it until now (manual test matrix scenario 5
        // used to log this as an explicit Fail: total silence).
        const QString what = (reason == StopReason::Detached)
            ? tr("The scope was disconnected.")
            : tr("The scope stopped.");
        // Spec 10.3's Linux EACCES row. It used to be checked on the
        // `!m_source->start()` path, which activation's asynchrony makes
        // unreachable, and against a hardcoded /dev/video0 -- so a real
        // permission problem on a scope enumerating as /dev/video1 (any
        // machine with a built-in webcam) produced the generic wording.
        // The Error path is where a denial actually surfaces, and
        // deviceNode() is the node the device actually has.
        QString message = permissionHintForError(reason);
        if (message.isEmpty())
            message = tr("%1 Nothing was being recorded. Reconnect the scope to "
                         "continue.").arg(what);

        // The driver's detail names the real cause -- another app holding
        // the device, a bandwidth failure, a vanished node -- and this is
        // the only place that information exists. Dropping it is what turns
        // a diagnosable fault into a bare "failed to open" (spec 10.3). This
        // must stay a local (non-member) append: sourceLost has to remain
        // the last statement on this path (see the comment above) since a
        // consumer's handler may delete `this` synchronously.
        if (!detail.isEmpty())
            message += tr(" Reported cause: %1.").arg(detail);

        emit status(message);
        emit sourceLost(message, reason);
    }
}


QString CaptureController::permissionHintForError(StopReason reason) const {
    const QString node = m_source->deviceNode();
    // Windows and macOS device ids are opaque handles, not paths, so there
    // is nothing to stat. Their equivalent -- a privacy block -- is
    // handled by CameraAccessPolicy::activationFailureMayBeAccessDenied().
    if (node.isEmpty() || !node.startsWith(QLatin1Char('/'))) return {};

    const QFileInfo info(node);
    // The decision itself is pure and lives in StopClassification, where it
    // has unit tests that run on every platform -- including this project's
    // only available one, which cannot execute the Linux branch at all.
    if (!StopClassification::isDeviceNodePermissionProblem(
            reason, info.exists(), info.isReadable()))
        return {};

    // Camera nodes are usually root:video 0660, so a user not in the video
    // group gets exactly this failure with no indication why. This is the
    // one place root is unavoidable, so the app must name the fix rather
    // than leave the technician guessing (spec 4).
    return tr("No permission to read the camera device %1. Nothing was saved. "
              "Run: sudo usermod -aG video $USER   then log out and back in.")
        .arg(node);
}
