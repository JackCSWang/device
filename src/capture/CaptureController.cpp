#include "capture/CaptureController.h"
#include "output/IRecorder.h"
#include "output/SnapshotWriter.h"
#include "storage/CaptureNaming.h"
#include "storage/DiskPolicy.h"
#include "storage/OutputLocation.h"
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QVideoSink>

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
                // which a detach- or stall-interrupted recording can be
                // reported without asserting something not yet true. Exactly
                // one status message reaches the user either way.
                if (m_pendingInterruption == RecordingInterruption::Detach) {
                    m_pendingInterruption = RecordingInterruption::None;
                    const QString message =
                        tr("The scope was disconnected. The recording was saved as %1. "
                           "Reconnect the scope to continue.").arg(QFileInfo(path).fileName());
                    // sourceLost may destroy this controller (a direct,
                    // same-thread connection can delete it synchronously,
                    // and it can even destroy the QtRecorder whose own
                    // finished() emission is still unwinding beneath this
                    // lambda) -- so it must be the very last thing this
                    // path touches `this` for. Nothing may follow it.
                    emit status(message);
                    emit sourceLost(message);
                } else if (m_pendingInterruption == RecordingInterruption::Stall) {
                    m_pendingInterruption = RecordingInterruption::None;
                    emit status(tr("The video stream stopped, so the recording was ended and "
                                   "saved as %1. Recording will not resume automatically.")
                                    .arg(QFileInfo(path).fileName()));
                } else {
                    emit status(tr("Recording saved as %1.").arg(QFileInfo(path).fileName()));
                }
            });
    connect(m_recorder, &IRecorder::failed, this,
            [this](const QString&, const QString& why) {
                if (m_pendingInterruption == RecordingInterruption::Detach) {
                    m_pendingInterruption = RecordingInterruption::None;
                    const QString message =
                        tr("The scope was disconnected. The recording could not be saved: %1")
                            .arg(why);
                    // See the matching comment in the finished() handler
                    // above: sourceLost must be last.
                    emit status(message);
                    emit sourceLost(message);
                } else if (m_pendingInterruption == RecordingInterruption::Stall) {
                    m_pendingInterruption = RecordingInterruption::None;
                    emit status(tr("The video stream stopped, so the recording was ended. It "
                                   "could not be saved: %1").arg(why));
                } else {
                    emit status(why);
                }
            });

    m_firstFrameTimer.setSingleShot(true);
    m_firstFrameTimer.setInterval(FirstFrameTimeoutMs);
    connect(&m_firstFrameTimer, &QTimer::timeout, this, [this] {
        // open() succeeded but nothing streamed -- the classic shared-hub
        // isochronous bandwidth failure (spec 10.2).
        emit firstFrameTimedOut();
        emit status(tr("No video received from the scope. Nothing was saved. "
                       "Try a direct USB port instead of a hub, or a lower resolution."));
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

bool CaptureController::isRecording() const {
    return m_recorder && m_recorder->isRecording();
}

void CaptureController::begin() {
    OutputLocation::ensureExists(m_outputDir);
    m_sawFirstFrame = false;
    if (!m_source->start()) {
#ifdef Q_OS_LINUX
        // On Linux, camera nodes are usually root:video 0660, so a user not
        // in the video group gets exactly this failure with no indication
        // why. This is the one place root is unavoidable, so the app must
        // name the fix rather than leave the technician guessing (spec §4).
        if (!QFileInfo(QStringLiteral("/dev/video0")).isReadable()) {
            emit status(tr("No permission to read the camera device. Nothing was saved. "
                           "Run: sudo usermod -aG video $USER   then log out and back in."));
            return;
        }
#endif
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
        m_writer->write(frame, timestampUs, reserveName(QStringLiteral("jpg")));
    }

    if (m_recorder->isRecording()) {
        if (DiskPolicy::mustStopRecording(DiskPolicy::freeBytesFor(m_outputDir))) {
            emit status(tr("Storage is nearly full. Stopping and saving the recording."));
            m_recorder->finalizeAndStop();
        } else {
            m_recorder->feed(frame, timestampUs);
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

void CaptureController::startRecording() {
    if (isRecording()) return;
    if (!DiskPolicy::canStartRecording(DiskPolicy::freeBytesFor(m_outputDir))) {
        emit status(tr("Not enough free storage to record. Nothing was saved. "
                       "Free at least 500 MB and try again."));
        return;
    }
    const QString path = reserveName(QStringLiteral("mp4"));
    const QSize size = m_source->frameSize();
    if (!m_recorder->start(path, size.isEmpty() ? QSize(1920, 1080) : size, 30.0)) {
        emit status(tr("Could not start recording. Nothing was saved."));
        return;
    }
    // A detach or stall from a *previous* recording may still be waiting
    // on an outcome here (the finished()/failed() handler that would
    // normally clear m_pendingInterruption hasn't run yet). This fresh
    // recording must not inherit that flag: without this reset, this
    // recording's own, unrelated finished() would later be reported as
    // "the scope was disconnected. The recording was saved as <this file>"
    // -- the original false claim, just relocated onto a healthy take.
    m_pendingInterruption = RecordingInterruption::None;
    m_pendingRecordingPath = path;
    emit status(tr("Recording to %1.").arg(QFileInfo(path).fileName()));
}

void CaptureController::stopRecording() {
    if (!isRecording()) return;
    m_recorder->finalizeAndStop();
}

void CaptureController::onSourceStopped(StopReason reason, const QString& detail) {
    Q_UNUSED(detail)
    m_firstFrameTimer.stop();
    m_stallTimer.stop();
    m_snapshotArmed = false;
    // Every member write this function needs happens up front, before
    // anything that could destroy `this` -- see the two `return`s below.
    m_pendingRecordingPath.clear();

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
        if (reason == StopReason::Detached)
            m_pendingInterruption = RecordingInterruption::Detach;
        else if (stallInitiated)
            m_pendingInterruption = RecordingInterruption::Stall;
        // finalizeAndStop() can synchronously drive the recorder all the way
        // to finished()/failed(), which -- since m_pendingInterruption is
        // now set -- can itself emit sourceLost() and let a consumer delete
        // this controller before this call even returns. `this` must not be
        // touched again on this path; the destructor already ran by the
        // time control gets back here if that happened.
        m_recorder->finalizeAndStop();
        return;
    }

    if (reason == StopReason::Detached) {
        const QString message = tr("The scope was disconnected. Nothing was being recorded. "
                                   "Reconnect the scope to continue.");
        // sourceLost may destroy this controller synchronously (a direct,
        // same-thread connection) -- it must be the last statement on this
        // path too.
        emit status(message);
        emit sourceLost(message);
    }
}
