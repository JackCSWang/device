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
                emit status(tr("Recording saved as %1.").arg(QFileInfo(path).fileName()));
            });
    connect(m_recorder, &IRecorder::failed, this,
            [this](const QString&, const QString& why) { emit status(why); });

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

    const bool wasRecording = isRecording();
    const QString path = m_pendingRecordingPath;

    // Finalize before reporting anything. A truncated MP4 has no moov atom
    // and will not open, so the evidence is simply gone (spec 10.1).
    if (wasRecording) m_recorder->finalizeAndStop();

    if (reason == StopReason::Detached) {
        const QString message = wasRecording
            ? tr("The scope was disconnected. The recording was saved as %1. "
                 "Reconnect the scope to continue.").arg(QFileInfo(path).fileName())
            : tr("The scope was disconnected. Nothing was being recorded. "
                 "Reconnect the scope to continue.");
        emit sourceLost(message);
        emit status(message);
    }
    m_pendingRecordingPath.clear();
}
