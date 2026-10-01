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
            focusPolicy: Qt.NoFocus   // Space belongs to the snapshot shortcut
            // The shortcut is useless if nobody knows it is there.
            ToolTip.visible: hovered
            ToolTip.text: qsTr("Snapshot (Space)")
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
            focusPolicy: Qt.NoFocus   // Space belongs to the snapshot shortcut
            highlighted: AppContext.recording
            onClicked: AppContext.toggleRecording()
        }

        Item { Layout.fillWidth: true }

        // N1 fix: the user's own way back to the picker. A remembered
        // choice is only ever recorded from an explicit selectDevice(), but
        // the technician still needs a way to revisit it -- e.g. a scope
        // that was misidentified, or a second camera attached after the
        // fact. Reachable whenever more than one device is present, not
        // only when nothing is currently open.
        Button {
            text: qsTr("Change scope")
            visible: AppContext.deviceNames.length > 1
            Layout.preferredHeight: 56
            focusPolicy: Qt.NoFocus   // Space belongs to the snapshot shortcut
            onClicked: AppContext.changeScope()
        }

        Label {
            text: qsTr("%1x").arg(AppContext.transform.zoom.toFixed(1))
            font.pixelSize: 18
        }

        Button {
            text: qsTr("Fit")
            Layout.preferredHeight: 56
            focusPolicy: Qt.NoFocus   // Space belongs to the snapshot shortcut
            onClicked: AppContext.resetView()
        }

        Button {
            text: qsTr("Open folder")
            Layout.preferredHeight: 56
            focusPolicy: Qt.NoFocus   // Space belongs to the snapshot shortcut
            onClicked: AppContext.openOutputFolder()
        }
    }
}
