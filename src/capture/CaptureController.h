#pragma once
#include "core/ICaptureSource.h"
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVideoFrame>

class IRecorder;
class SnapshotWriter;
class QVideoSink;

// Fans frames out to the display sink and, when armed, to the snapshot
// writer and recorder. Owns the watchdogs and the detach path.
//
// It does not know about zoom, and has no way to learn about it: the view
// transform lives entirely in the UI layer, so a capture written here is
// always the full sensor frame (spec 7.2, 9).
class CaptureController : public QObject {
    Q_OBJECT
public:
    static constexpr int FirstFrameTimeoutMs = 3000;
    static constexpr int StallTimeoutMs = 5000;

    CaptureController(ICaptureSource* source, IRecorder* recorder,
                      SnapshotWriter* writer, QString outputDir,
                      QObject* parent = nullptr);

    void begin();
    void takeSnapshot();
    void startRecording();
    void stopRecording();

    bool isRecording() const;
    QVideoSink* displaySink() const { return m_displaySink; }

signals:
    void status(QString message);
    void snapshotSaved(QString path);
    void recordingSaved(QString path);
    void sourceLost(QString message);
    void firstFrameTimedOut();
    // Emitted once the format preference chain is exhausted with no frame
    // ever seen -- the watchdog tried every advertised format and gave up
    // (spec 10.2/10.3). advertised is whatever formatDescriptions() reports,
    // possibly empty for a source that doesn't implement it.
    void formatsExhausted(QStringList advertised);

private:
    // Why a recorder was cut short mid-take, tracked so the *outcome*
    // (finished/failed -- always asynchronous; see IRecorder's contract)
    // can be reported accurately instead of asserted the instant
    // finalizeAndStop() is merely requested (spec 10.1: claiming evidence
    // was saved before that is actually known is worse than saying
    // nothing, because the technician stops looking for it).
    enum class RecordingInterruption { None, Detach, Stall };

    void onFrame(const QVideoFrame& frame, qint64 timestampUs);
    void onSourceStopped(StopReason reason, const QString& detail);
    QString reserveName(const QString& extension) const;

    ICaptureSource* m_source;
    IRecorder* m_recorder;
    SnapshotWriter* m_writer;
    QVideoSink* m_displaySink;
    QString m_outputDir;

    QTimer m_firstFrameTimer;
    QTimer m_stallTimer;
    bool m_sawFirstFrame = false;
    bool m_snapshotArmed = false;
    QString m_pendingRecordingPath;

    // Set immediately before the stall-watchdog handler calls
    // m_source->stop(), and consumed by onSourceStopped -- which that call
    // can re-enter synchronously -- to tell a stall-initiated stop apart
    // from any other StopReason::Requested. Always cleared by
    // onSourceStopped before it returns.
    bool m_stallStopping = false;

    // Set by onSourceStopped when a detach or a stall cuts a recording
    // short, cleared by the finished()/failed() handler that reports the
    // outcome to the user. None the rest of the time (including a normal
    // user-requested stopRecording(), which needs no deferred message).
    RecordingInterruption m_pendingInterruption = RecordingInterruption::None;

    // Names handed out by reserveName() but not yet necessarily written to
    // disk: SnapshotWriter::write() queues the actual encode on a thread
    // pool and returns immediately, so two reserveName() calls issued back
    // to back (a technician double-tapping the shutter) can both run their
    // dir.exists() check before the first file lands on disk. Without this,
    // CaptureNaming's collision suffixing -- which only ever consults the
    // predicate it is given -- would not see the first name as taken and
    // would hand out the same name twice, exactly the silent overwrite
    // Task 4 exists to prevent (spec 10, this task's brief).
    mutable QSet<QString> m_reservedNames;
};
