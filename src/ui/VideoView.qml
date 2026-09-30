import QtQuick
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
        anchors.centerIn: parent
        width: root.width
        height: root.height
        fillMode: VideoOutput.PreserveAspectFit

        // All arithmetic lives in ViewTransformModel; these are bindings only.
        scale: AppContext.transform.zoom
        x: (root.width  - width)  / 2 + AppContext.transform.contentX
        y: (root.height - height) / 2 + AppContext.transform.contentY

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

    PinchHandler {
        target: null
        onActiveScaleChanged: {
            if (activeScale > 0)
                AppContext.transform.zoomAt(activeScale, centroid.position.x, centroid.position.y)
        }
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
