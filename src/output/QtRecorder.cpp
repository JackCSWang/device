#include "output/QtRecorder.h"
#include <QFile>
#include <QMediaFormat>
#include <QUrl>

QtRecorder::QtRecorder(QObject* parent) : IRecorder(parent) {}

bool QtRecorder::start(const QString& path, const QSize& size, qreal frameRate) {
    if (m_recording) return false;

    m_session  = std::make_unique<QMediaCaptureSession>();
    m_recorder = std::make_unique<QMediaRecorder>();
    m_input    = std::make_unique<QVideoFrameInput>();

    QMediaFormat format;
    format.setFileFormat(QMediaFormat::MPEG4);
    format.setVideoCodec(QMediaFormat::VideoCodec::H264);
    m_recorder->setMediaFormat(format);
    m_recorder->setVideoResolution(size);
    m_recorder->setVideoFrameRate(frameRate);
    m_recorder->setQuality(QMediaRecorder::HighQuality);
    m_recorder->setOutputLocation(QUrl::fromLocalFile(path));

    m_session->setVideoFrameInput(m_input.get());
    m_session->setRecorder(m_recorder.get());

    connect(m_recorder.get(), &QMediaRecorder::recorderStateChanged,
            this, &QtRecorder::onRecorderStateChanged);
    connect(m_recorder.get(), &QMediaRecorder::errorOccurred, this,
            [this](QMediaRecorder::Error, const QString& s) {
                // Latch the error so onRecorderStateChanged's later
                // StoppedState transition never also emits finished() for
                // this same broken recording (spec 10.1: reporting success
                // for evidence that never actually saved is worse than a
                // truncated file, because the technician isn't told).
                m_errored = true;

                // isRecording() must go false now, not whenever a caller
                // next happens to call finalizeAndStop() -- IRecorder's
                // contract requires it, and CaptureController's detach
                // handler in particular depends on it: a caller that still
                // believes a recording is in flight on a recorder that has
                // already failed defers reporting anything, waiting for an
                // outcome that was already reported right here and will
                // never come again. Route through finalizeAndStop() itself
                // -- the one place that stops the underlying QMediaRecorder
                // and clears m_recording -- rather than duplicating that
                // logic here, so a caller that also calls finalizeAndStop()
                // explicitly later (or one already in flight right now, on
                // the stack below this handler) finds it already stopped
                // and safely no-ops: no double stop, and the muxer still
                // gets closed even though the recording failed.
                finalizeAndStop();

                emit failed(m_path, tr("Recording failed: %1. The file may be "
                                       "incomplete. Check available storage, "
                                       "then start a new recording.").arg(s));
            });

    m_path = path;
    m_firstPtsUs = m_lastPtsUs = -1;
    m_framesFed = 0;
    m_errored = false;
    m_recording = true;
    m_recorder->record();
    return true;
}

void QtRecorder::feed(const QVideoFrame& frame, qint64 ptsUs) {
    if (!m_recording || !m_input || !frame.isValid()) return;

    if (m_firstPtsUs < 0) m_firstPtsUs = ptsUs;
    m_lastPtsUs = ptsUs;

    QVideoFrame stamped = frame;
    stamped.setStartTime(ptsUs - m_firstPtsUs);
    stamped.setEndTime(ptsUs - m_firstPtsUs);

    if (m_input->sendVideoFrame(stamped)) ++m_framesFed;
    // A rejected frame means the encoder is saturated. Dropping it is correct:
    // timestamps carry the timing, so playback speed stays honest.
}

void QtRecorder::finalizeAndStop() {
    if (!m_recording) return;
    m_recording = false;
    if (m_recorder) m_recorder->stop();
}

void QtRecorder::onRecorderStateChanged(QMediaRecorder::RecorderState state) {
    if (state != QMediaRecorder::StoppedState) return;

    // errorOccurred already emitted failed() with m_path for this recording.
    // Emitting finished() as well would tell a consumer -- most critically
    // Task 9's detach handler -- that a broken recording was saved, which is
    // exactly the outcome spec 10.1 exists to prevent. Exactly one of
    // finished/failed must fire per recording, never both.
    if (m_errored) return;

    const qint64 durationUs =
        (m_framesFed > 0 && m_lastPtsUs >= m_firstPtsUs) ? m_lastPtsUs - m_firstPtsUs : 0;

    // A zero-frame recording produces a file that looks like a take and will
    // not open. Remove it rather than hand someone broken evidence.
    if (m_framesFed == 0) {
        QFile::remove(m_path);
        emit failed(m_path, tr("No frames were recorded, so no file was saved. "
                               "Check the scope is connected and delivering "
                               "video, then try again."));
        return;
    }
    emit finished(m_path, durationUs);
}
