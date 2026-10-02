import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtMultimedia
import microscope

Item {
    id: root
    clip: true

    onWidthChanged: AppContext.transform.setViewportSize(Qt.size(width, height))
    onHeightChanged: AppContext.transform.setViewportSize(Qt.size(width, height))

    Rectangle { anchors.fill: parent; color: "#101014" }

    VideoOutput {
        id: output
        // No anchors.centerIn here: it and the x/y bindings below both
        // target the same axes and differ by exactly the pan offset.
        // QQuickAnchors re-applies setPos() on every geometry change, so on
        // a resize the anchor can silently win and drop the pan offset
        // until the next gesture. The x/y arithmetic below already centres.
        //
        // On a quarter turn the item's OWN width/height swap, so that after
        // the rotation transform below it occupies the viewport the right
        // way round. Rotating without swapping would letterbox against the
        // wrong axis and leave the fit scale disagreeing with
        // ViewTransform, which AppContext feeds the oriented frame size.
        readonly property bool quarterTurned: AppContext.orientation.degrees === 90
                                           || AppContext.orientation.degrees === 270
        width:  quarterTurned ? root.height : root.width
        height: quarterTurned ? root.width  : root.height
        fillMode: VideoOutput.PreserveAspectFit

        // All arithmetic lives in ViewTransformModel; these are bindings only.
        scale: AppContext.transform.zoom
        x: (root.width  - width)  / 2 + AppContext.transform.contentX
        y: (root.height - height) / 2 + AppContext.transform.contentY

        // Orientation is applied to the DISPLAY here and to the pixels of
        // saved files in Orientation::apply(). The two must agree: mirror
        // first, then rotate. Qt applies `transform` entries before the
        // `rotation` property, so listing the mirror in `transform` and the
        // turn in `rotation` gives exactly that order -- do not merge them
        // into one list without re-checking which runs first.
        rotation: AppContext.orientation.degrees
        transform: Scale {
            origin.x: output.width / 2
            origin.y: output.height / 2
            xScale: AppContext.orientation.mirrored ? -1 : 1
        }

        // VideoOutput.videoSink is read-only: it is the item's own render
        // target, so frames must be pushed into it, not assigned out of
        // AppContext. AppContext relays CaptureController's frames here.
        Component.onCompleted: AppContext.setVideoSink(output.videoSink)
    }

    Connections {
        target: AppContext
        function onPipelineChanged() {
            AppContext.setVideoSink(output.videoSink)
        }
    }

    // Spec 8.5 step 1's "otherwise prompt" half. With more than one video
    // input present and none remembered, nothing is opened until the
    // technician says which one is the scope. Deliberately a list of the
    // OS's own device descriptions and nothing cleverer: the microscope on
    // the development machine enumerates as "HD camera" and the laptop's
    // built-in one as "Integrated Camera", so any keyword heuristic picks
    // the wrong device on the very hardware that matters.
    Rectangle {
        id: picker
        visible: AppContext.needsDeviceChoice
        anchors.centerIn: parent
        width: Math.min(root.width - 40, 520)
        height: Math.min(root.height - 40, pickerLayout.implicitHeight + 40)
        color: "#1a1a22"
        border { color: "#3a3a4a"; width: 1 }
        radius: 6

        ColumnLayout {
            id: pickerLayout
            anchors { fill: parent; margins: 20 }
            spacing: 12

            Label {
                Layout.fillWidth: true
                text: qsTr("Select a scope")
                color: "#f0f0f0"
                font.pixelSize: 20
                wrapMode: Text.WordWrap
            }
            Label {
                Layout.fillWidth: true
                text: qsTr("More than one camera is connected. Choose the microscope; "
                           + "this machine will remember it.")
                color: "#b0b0b8"
                wrapMode: Text.WordWrap
            }

            Repeater {
                model: AppContext.deviceNames
                delegate: Button {
                    required property string modelData
                    required property int index
                    Layout.fillWidth: true
                    Layout.preferredHeight: 56       // large enough for gloved hands
                    text: modelData
                    onClicked: AppContext.selectDevice(index)
                }
            }
        }
    }

    PinchHandler {
        target: null
        // activeScale is cumulative since the gesture began (1.0 at pinch
        // start), but zoomAt() multiplies by its factor -- feeding it the
        // running total in as a per-event factor would compound every
        // intermediate total into the zoom. scaleChanged's own delta
        // parameter is the actual per-event factor.
        onScaleChanged: (delta) => AppContext.transform.zoomAt(delta, centroid.position.x, centroid.position.y)
    }

    WheelHandler {
        acceptedModifiers: Qt.NoModifier
        onWheel: (event) => {
            const factor = event.angleDelta.y > 0 ? 1.15 : 1 / 1.15
            AppContext.transform.zoomAt(factor, event.x, event.y)
        }
    }

    DragHandler {
        target: null
        enabled: AppContext.transform.canPan
        property point last: Qt.point(0, 0)
        onActiveChanged: if (active) last = centroid.position
        onCentroidChanged: {
            if (!active) return
            AppContext.transform.panBy(centroid.position.x - last.x,
                                       centroid.position.y - last.y)
            last = centroid.position
        }
    }
}
