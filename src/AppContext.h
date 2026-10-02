#pragma once
#include <QObject>
#include <QString>
#include <QStringList>
#include <qqmlintegration.h>
#include <memory>
// Q_PROPERTY and Q_INVOKABLE pointer types must be complete wherever moc's
// generated code for this class is compiled (only this header, not
// AppContext.cpp's includes), or Qt 6.8's automatic metatype table
// generation fails to compile -- so these two, unlike the pImpl-only members
// below, need real includes rather than forward declarations.
#include <QSizeF>
#include <QVideoSink>
#include "view/ViewTransformModel.h"
#include "view/OrientationModel.h"

class CaptureSession;
class DeviceRegistry;

// Wires the pipeline together and exposes exactly what QML needs. Holds no
// logic of its own -- logic lives in microscope_core, where it is tested.
//
// That claim used to be false. The whole capture lifecycle (open, teardown
// ordering, the reopen decision, the recovery policy, device selection) was
// written here, in the app target, and three Criticals hid in it through
// fourteen reviews because no test can reach this file. It now lives in
// CaptureSession. What is left here is genuinely wiring: Q_PROPERTY relays,
// QSettings persistence of the remembered device id, and
// openCameraPrivacySettings()'s per-platform URL dispatch.
class AppContext : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(ViewTransformModel* transform READ transform CONSTANT)
    Q_PROPERTY(OrientationModel* orientation READ orientation CONSTANT)
    // Current device/pipeline state. Replaced by each new device-state
    // message, so a transient error does not sit over a healthy pipeline
    // forever.
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusTextChanged)
    // The sticky capture-outcome line: the last recording or snapshot that
    // saved (or failed to). No device-state message can overwrite it, which
    // is what keeps a saved recording's filename on screen after the
    // pipeline is torn down and the reopen attempt reports "No scope
    // detected" (spec 10.1).
    Q_PROPERTY(QString lastOutcomeText READ lastOutcomeText NOTIFY lastOutcomeTextChanged)
    Q_PROPERTY(bool recording READ recording NOTIFY recordingChanged)
    Q_PROPERTY(bool hasDevice READ hasDevice NOTIFY pipelineChanged)
    // True once frames are actually arriving. Recording is gated on this,
    // not merely on a device being open: a take started before the first
    // frame has a fabricated resolution and can contain nothing.
    Q_PROPERTY(bool hasVideo READ hasVideo NOTIFY pipelineChanged)
    // True when the OS enumerated at least one camera at startup, this app
    // has never successfully opened one, and the current list is empty --
    // or when an activation failed with the device still enumerated, which
    // is how a Windows camera-privacy block actually presents (spec 10.3).
    Q_PROPERTY(bool cameraAccessDenied READ cameraAccessDenied NOTIFY pipelineChanged)
    // True when more than one video input is present and none has been
    // chosen (spec 8.5 step 1: "auto-open if exactly one scope, otherwise
    // prompt").
    Q_PROPERTY(bool needsDeviceChoice READ needsDeviceChoice NOTIFY pipelineChanged)
    Q_PROPERTY(QStringList deviceNames READ deviceNames NOTIFY deviceListChanged)

public:
    explicit AppContext(QObject* parent = nullptr);
    ~AppContext() override;

    ViewTransformModel* transform() const { return m_transform; }
    OrientationModel* orientation() const { return m_orientation; }
    QString statusText() const;
    QString lastOutcomeText() const;
    bool recording() const;
    bool hasDevice() const;
    bool hasVideo() const;
    bool cameraAccessDenied() const;
    bool needsDeviceChoice() const;
    QStringList deviceNames() const;

    Q_INVOKABLE void snapshot();
    Q_INVOKABLE void toggleRecording();
    Q_INVOKABLE void resetView();
    Q_INVOKABLE void openOutputFolder();
    // Index into deviceNames(). Opens that scope and remembers it.
    Q_INVOKABLE void selectDevice(int index);
    // User-initiated recovery after a device error. The Error recovery path
    // deliberately does not retry on its own (see CaptureSession).
    Q_INVOKABLE void retry();
    // N1 fix: the user's own way back to the device picker, reachable
    // whenever more than one scope is present -- not only when nothing is
    // open. The decision and the teardown live in CaptureSession; this is a
    // relay.
    Q_INVOKABLE void changeScope();
    // Opens the OS's camera-privacy settings page so the technician can
    // grant access without hunting for it themselves (spec 10.3).
    Q_INVOKABLE void openCameraPrivacySettings();

    // VideoOutput.videoSink is CONSTANT (read-only) in QtQuick's QML API --
    // it is the render target VideoOutput itself owns, not something an
    // external sink can be assigned into. QML hands its item's own sink here
    // so the display path can be wired the only direction Qt Multimedia
    // actually supports: something pushes frames INTO it.
    //
    // There is deliberately no `videoSink` Q_PROPERTY to read back. One
    // existed, unused, and advertised exactly the wiring direction Qt does
    // not support -- the confusion that caused the live-view defect in the
    // first place.
    Q_INVOKABLE void setVideoSink(QVideoSink* sink);

signals:
    void pipelineChanged();
    void statusTextChanged();
    void lastOutcomeTextChanged();
    void recordingChanged();
    void deviceListChanged();

private:
    DeviceRegistry* m_registry = nullptr;
    ViewTransformModel* m_transform = nullptr;
    OrientationModel* m_orientation = nullptr;
    // The sensor's own frame size, before orientation. Kept so a rotation
    // can recompute what the view should fit without waiting for the next
    // frame to arrive -- which on a stalled or detached scope never comes.
    QSizeF m_rawFrameSize;
    std::unique_ptr<CaptureSession> m_session;
    QMetaObject::Connection m_videoRelay;
    QString m_outputDir;
};
