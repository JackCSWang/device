#pragma once
#include "core/ICaptureSource.h"
#include <QObject>
#include <QSet>
#include <QString>
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

private:
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
