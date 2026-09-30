#pragma once
#include <QObject>
#include <QSize>
#include <QString>
#include <QVideoFrame>

// Frames are pushed in with their capture timestamp as PTS, so video is
// written variable-frame-rate. If the encoder falls behind on weak hardware
// the result is a lower frame rate that still plays at correct wall-clock
// speed -- not a file that plays fast, which for inspection evidence would
// be actively misleading (spec 8.4).
//
// Contract for implementations, both load-bearing for CaptureController's
// detach/stall handling (spec 10.1):
//
//  1. After finalizeAndStop() ends a recording that was genuinely in
//     flight, exactly one of finished()/failed() must eventually be
//     emitted -- never both, never neither. A caller that defers reporting
//     an interruption until it learns the real outcome (as
//     CaptureController does) depends on that outcome actually arriving;
//     silence leaves the interruption unreported forever.
//  2. Once a recording has failed -- whether reported via finalizeAndStop()'s
//     own outcome, or from an error latched earlier in the take, before
//     finalizeAndStop() was ever called -- isRecording() must report false
//     from that point on. A caller must never be able to observe a
//     recording as still in flight after this class already knows it is
//     dead, or a detach that follows will find nothing to react to and
//     defer to an outcome that (per rule 1) has already been reported and
//     will never come again.
class IRecorder : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    ~IRecorder() override = default;

    virtual bool start(const QString& path, const QSize& size, qreal frameRate) = 0;
    virtual void feed(const QVideoFrame& frame, qint64 ptsUs) = 0;

    // Drains the encoder and closes the muxer. An MP4 without its moov atom
    // is unplayable, so this must run even on unexpected detach (spec 10.1).
    virtual void finalizeAndStop() = 0;

    virtual bool isRecording() const = 0;

signals:
    void finished(QString path, qint64 durationUs);
    void failed(QString path, QString reason);
};
