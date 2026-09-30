import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import microscope

Pane {
    RowLayout {
        anchors.fill: parent
        spacing: 12

        Button {
            text: qsTr("Snapshot")
            enabled: AppContext.hasDevice
            Layout.preferredHeight: 56       // large enough for gloved hands
            onClicked: AppContext.snapshot()
        }

        Button {
            text: AppContext.recording ? qsTr("Stop recording") : qsTr("Record")
            // Must-fix minor 9: hasVideo, not hasDevice. A recording
            // started before the first frame has no real resolution to
            // encode at and cannot contain anything;
            // CaptureController::startRecording() refuses it, and the
            // button should not invite it. Snapshot stays on hasDevice so
            // its own "no video yet" message is still reachable.
            enabled: AppContext.hasVideo
            Layout.preferredHeight: 56
            highlighted: AppContext.recording
            onClicked: AppContext.toggleRecording()
        }

        Item { Layout.fillWidth: true }

        Label {
            text: qsTr("%1x").arg(AppContext.transform.zoom.toFixed(1))
            font.pixelSize: 18
        }

        Button {
            text: qsTr("Fit")
            Layout.preferredHeight: 56
            onClicked: AppContext.resetView()
        }

        Button {
            text: qsTr("Open folder")
            Layout.preferredHeight: 56
            onClicked: AppContext.openOutputFolder()
        }
    }
}
