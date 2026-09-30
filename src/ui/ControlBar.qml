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
            enabled: AppContext.hasDevice
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
