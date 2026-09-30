import QtQuick
import QtQuick.Controls
import microscope

Rectangle {
    visible: AppContext.statusText.length > 0
    implicitHeight: visible ? label.implicitHeight + 20 : 0
    color: AppContext.hasDevice ? "#1d2b1d" : "#2b1d1d"

    Label {
        id: label
        anchors { fill: parent; margins: 10 }
        text: AppContext.statusText
        wrapMode: Text.WordWrap
        color: "#f0f0f0"
    }
}
