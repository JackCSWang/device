#pragma once
#include "output/IRecorder.h"
#include <QMediaCaptureSession>
#include <QMediaRecorder>
#include <QVideoFrameInput>
#include <memory>

class QtRecorder : public IRecorder {
    Q_OBJECT
public:
    explicit QtRecorder(QObject* parent = nullptr);

    bool start(const QString& path, const QSize& size, qreal frameRate) override;
    void feed(const QVideoFrame& frame, qint64 ptsUs) override;
    void finalizeAndStop() override;
    bool isRecording() const override { return m_recording; }

private:
    void onRecorderStateChanged(QMediaRecorder::RecorderState state);

    std::unique_ptr<QMediaCaptureSession> m_session;
    std::unique_ptr<QMediaRecorder> m_recorder;
    std::unique_ptr<QVideoFrameInput> m_input;
    QString m_path;
    bool m_recording = false;
    qint64 m_firstPtsUs = -1;
    qint64 m_lastPtsUs = -1;
    int m_framesFed = 0;
};
