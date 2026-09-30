import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    width: 1100
    height: 760
    visible: true
    title: qsTr("Microscope")

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        StatusBanner { Layout.fillWidth: true }
        VideoView { Layout.fillWidth: true; Layout.fillHeight: true }
        ControlBar { Layout.fillWidth: true }
    }
}
