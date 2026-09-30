#pragma once
#include <QObject>
#include <QString>

// The status-precedence rule, made structural.
//
// Spec 10.1: "Naming the saved file is required, not decorative. 'Device
// disconnected' alone leaves the technician assuming the take was lost."
// The old single-string status could not honour that, because the very
// sequence the spec cares about overwrites it: CaptureController reports
// "The scope was disconnected. The recording was saved as
// scope_20260930_141233.mp4", the pipeline is torn down, and on the next
// event-loop turn the reopen attempt replaces it with "No scope detected."
// The filename -- the entire point of the message -- was gone before a
// technician could read it.
//
// So there are two lines, with a precedence rule between them:
//
//  * deviceStatus is *current device state*. Each new device-state message
//    replaces the previous one, which is also why a transient error no
//    longer sits on screen forever over a healthy pipeline: the next
//    device-state message clears it.
//
//  * lastOutcome is a *capture outcome* -- a recording or snapshot that
//    saved, or one that failed to save. It is sticky: no device-state or
//    idle message can touch it. Only another outcome replaces it. It
//    therefore survives past the status that follows it, which is what a
//    technician actually needs ("Last saved: scope_20260930_141233.mp4"
//    still on screen next to "No scope detected").
class StatusModel : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;

    QString deviceStatus() const { return m_deviceStatus; }
    QString lastOutcome() const { return m_lastOutcome; }

    void setDeviceStatus(const QString& text);

    // A capture outcome. Sticky -- see the class comment.
    void setLastOutcome(const QString& text);

signals:
    void deviceStatusChanged();
    void lastOutcomeChanged();

private:
    QString m_deviceStatus;
    QString m_lastOutcome;
};
