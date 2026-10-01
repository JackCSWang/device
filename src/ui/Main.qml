import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import microscope

ApplicationWindow {
    width: 1100
    height: 760
    visible: true
    title: qsTr("Microscope")

    // Keyboard path to snapshot. A field operator holds the scope against
    // the sample with one hand and often cannot also aim a cursor at the
    // on-screen button, so this is the practical way to capture evidence
    // one-handed. It also makes any USB footswitch or presenter remote
    // work with no further code, since those enumerate as HID keyboards
    // and simply send a keystroke -- unlike this scope's own side button,
    // which exposes no HID interface (see docs/manual-test-matrix.md).
    //
    // Window-scoped (the Shortcut default) so it fires wherever focus sits.
    //
    // autoRepeat MUST stay false. Qt defaults it to true, and a held key
    // would then fire once per repeat -- writing a JPEG each time, filling
    // the output folder with near-identical frames and burning through the
    // free-space budget DiskPolicy exists to defend.
    //
    // enabled mirrors the Snapshot button's own condition so the keyboard
    // and the button cannot disagree about when capture is possible.
    Shortcut {
        objectName: "snapshotShortcut"
        sequence: "Space"
        autoRepeat: false
        enabled: AppContext.hasDevice
        onActivated: AppContext.snapshot()
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        StatusBanner { Layout.fillWidth: true }
        VideoView { Layout.fillWidth: true; Layout.fillHeight: true }
        ControlBar { Layout.fillWidth: true }
    }
}
