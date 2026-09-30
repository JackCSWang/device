import QtQuick
import QtQuick.Controls
import microscope

Rectangle {
    visible: AppContext.statusText.length > 0
    implicitHeight: visible ? label.implicitHeight + 20 : 0
    color: AppContext.hasDevice ? "#1d2b1d" : "#2b1d1d"

    Label {
        id: label
        anchors {
            fill: parent
            margins: 10
            rightMargin: settingsButton.visible ? settingsButton.width + 20 : 10
        }
        text: AppContext.statusText
        wrapMode: Text.WordWrap
        color: "#f0f0f0"
    }

    Button {
        id: settingsButton
        visible: AppContext.cameraAccessDenied
        anchors { right: parent.right; verticalCenter: parent.verticalCenter; rightMargin: 10 }
        text: qsTr("Open camera settings")
        onClicked: AppContext.openCameraPrivacySettings()
    }
}
