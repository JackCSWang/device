import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import microscope

Rectangle {
    id: banner
    visible: AppContext.statusText.length > 0 || AppContext.lastOutcomeText.length > 0
    implicitHeight: visible ? lines.implicitHeight + 20 : 0
    color: AppContext.hasDevice ? "#1d2b1d" : "#2b1d1d"

    ColumnLayout {
        id: lines
        spacing: 4
        anchors {
            left: parent.left
            right: settingsButton.visible ? settingsButton.left : parent.right
            verticalCenter: parent.verticalCenter
            leftMargin: 10
            rightMargin: 10
        }

        // Current device state. Replaced whenever the device state changes.
        Label {
            Layout.fillWidth: true
            visible: AppContext.statusText.length > 0
            text: AppContext.statusText
            wrapMode: Text.WordWrap
            color: "#f0f0f0"
        }

        // The sticky capture-outcome line (spec 10.1). A saved recording or
        // snapshot names its file here, and no device-state message can
        // overwrite it -- so "Last saved: scope_20260930_141233.mp4" is
        // still on screen after a detach has already replaced the line
        // above with "No scope detected". Naming the saved file is required,
        // not decorative: the technician has to know where the evidence is.
        Label {
            Layout.fillWidth: true
            visible: AppContext.lastOutcomeText.length > 0
            text: AppContext.lastOutcomeText
            wrapMode: Text.WordWrap
            color: "#c8d8c8"
            font.bold: true
        }
    }

    Button {
        id: settingsButton
        visible: AppContext.cameraAccessDenied
        anchors { right: parent.right; verticalCenter: parent.verticalCenter; rightMargin: 10 }
        text: qsTr("Open camera settings")
        onClicked: AppContext.openCameraPrivacySettings()
    }
}
