#pragma once
#include <QObject>
#include <QString>
#include <qqmlintegration.h>
#include <memory>
// Q_PROPERTY pointer types must be complete wherever moc's generated code for
// this class is compiled (only this header, not AppContext.cpp's includes),
// or Qt 6.8's automatic metatype table generation fails to compile -- so
// these two, unlike the pImpl-only members below, need real includes rather
// than forward declarations.
#include <QVideoSink>
#include "view/ViewTransformModel.h"

class CaptureController;
class DeviceRegistry;
class ICaptureSource;
class IRecorder;
class SnapshotWriter;

// Wires the pipeline together and exposes exactly what QML needs. Holds no
// logic of its own -- logic lives in microscope_core, where it is tested.
class AppContext : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(QVideoSink* videoSink READ videoSink NOTIFY pipelineChanged)
    Q_PROPERTY(ViewTransformModel* transform READ transform CONSTANT)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusTextChanged)
    Q_PROPERTY(bool recording READ recording NOTIFY recordingChanged)
    Q_PROPERTY(bool hasDevice READ hasDevice NOTIFY pipelineChanged)

public:
    explicit AppContext(QObject* parent = nullptr);
    ~AppContext() override;

    QVideoSink* videoSink() const;
    ViewTransformModel* transform() const { return m_transform; }
    QString statusText() const { return m_statusText; }
    bool recording() const;
    bool hasDevice() const { return m_source != nullptr; }

    Q_INVOKABLE void snapshot();
    Q_INVOKABLE void toggleRecording();
    Q_INVOKABLE void resetView();
    Q_INVOKABLE void openOutputFolder();

    // VideoOutput.videoSink is CONSTANT (read-only) in QtQuick's QML API --
    // it is the render target VideoOutput itself owns, not something an
    // external sink can be assigned into. QML hands its item's own sink here
    // so the display path can be wired the only direction Qt Multimedia
    // actually supports: something pushes frames INTO it.
    Q_INVOKABLE void setVideoSink(QVideoSink* sink);

signals:
    void pipelineChanged();
    void statusTextChanged();
    void recordingChanged();

private:
    void openFirstAvailableDevice();
    void setStatus(const QString& text);

    DeviceRegistry* m_registry = nullptr;
    ViewTransformModel* m_transform = nullptr;
    SnapshotWriter* m_writer = nullptr;
    std::unique_ptr<IRecorder> m_recorder;
    std::unique_ptr<ICaptureSource> m_source;
    std::unique_ptr<CaptureController> m_controller;
    QMetaObject::Connection m_videoRelay;
    QString m_statusText;
    QString m_outputDir;
};
