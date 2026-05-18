import QtQuick 2.15

Item {
    id: root

    property int rpm:            0
    property int ledCount:       10
    property int greenStart:     4500
    property int yellowStart:    6000
    property int redStart:       6500
    property int flashStart:     6750
    property int flashIntervalMs: 80

    // RPM span covered by one LED in the green zone
    readonly property real rpmPerLed: (yellowStart - greenStart) / Math.max(ledCount, 1)

    readonly property int  litCount:   rpm < greenStart ? 0
                                     : Math.min(ledCount, Math.floor((rpm - greenStart) / rpmPerLed) + 1)
    readonly property bool isYellow:   rpm >= yellowStart && rpm < redStart
    readonly property bool isRed:      rpm >= redStart
    readonly property bool isFlashing: rpm >= flashStart

    readonly property color activeColor: isRed    ? "#ff2200"
                                       : isYellow ? "#ffcc00"
                                       : "#00dd44"

    property real flashAlpha: 1.0

    onIsFlashingChanged: if (!isFlashing) flashAlpha = 1.0

    SequentialAnimation on flashAlpha {
        running: root.isFlashing
        loops:   Animation.Infinite
        NumberAnimation { to: 0.1; duration: root.flashIntervalMs }
        NumberAnimation { to: 1.0; duration: root.flashIntervalMs }
    }

    Row {
        anchors.fill: parent
        spacing: 6

        Repeater {
            model: root.ledCount

            Rectangle {
                readonly property bool lit: index < root.litCount

                width:   (root.width - (root.ledCount - 1) * 6) / root.ledCount
                height:  root.height
                radius:  4
                color:   lit ? root.activeColor : "#181818"
                border.color: lit ? Qt.darker(root.activeColor, 1.5) : "#2a2a2a"
                border.width: 1
                opacity: lit && root.isFlashing ? root.flashAlpha : 1.0

                Rectangle {
                    visible: parent.lit
                    anchors { top: parent.top; left: parent.left; right: parent.right; margins: 1 }
                    height:  parent.height * 0.35
                    radius:  parent.radius
                    color:   "white"
                    opacity: 0.18
                }
            }
        }
    }
}
