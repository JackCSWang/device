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
