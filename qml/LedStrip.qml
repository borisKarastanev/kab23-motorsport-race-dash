import QtQuick 2.15

Item {
    id: root

    property int rpm:            0
    property int ledCount:       10
    property int flashIntervalMs: 80

    // Pair thresholds, outside-in (pair 0 = outermost, pair 4 = centre)
    // Pairs 0-1: green  |  Pairs 2-3: yellow  |  Pair 4: red + flash
    property int pair0Rpm:   5800
    property int pair1Rpm:   6000
    property int pair2Rpm:   6200
    property int pair3Rpm:   6500
    property int pair4Rpm:   6600
    property int allBlueRpm: 6750

    // Renders as if the limiter were this fraction of the configured one, so a
    // temporary derate (cold oil) can move every shift point at once without
    // rewriting — or persisting — the user's configured thresholds. Applied
    // here rather than at the call site so the "thresholds scale with the
    // limiter" rule stays in one place and callers pass one number, not six.
    // 1.0 = exactly as configured.
    property real limiterScale: 1.0

    readonly property var pairThresholds: [pair0Rpm, pair1Rpm, pair2Rpm, pair3Rpm, pair4Rpm]
                                          .map(v => Math.round(v * limiterScale))
    readonly property int scaledAllBlueRpm: Math.round(allBlueRpm * limiterScale)
    readonly property var pairBaseColors: ["#00dd44", "#00dd44", "#ffcc00", "#ffcc00", "#ff2200"]

    readonly property bool isAllBlue:        rpm >= scaledAllBlueRpm
    readonly property bool isCenterFlashing: rpm >= pairThresholds[4]

    property real flashAlpha: 1.0

    onIsCenterFlashingChanged: if (!isCenterFlashing) flashAlpha = 1.0

    SequentialAnimation on flashAlpha {
        running: root.isCenterFlashing
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
                // Mirror index so both halves resolve to the same pair threshold
                readonly property int pairIndex: index < (root.ledCount / 2)
                                                 ? index
                                                 : (root.ledCount - 1 - index)
                readonly property bool lit: root.rpm >= root.pairThresholds[pairIndex]

                readonly property color ledColor: {
                    if (!lit) return "#181818"
                    if (root.isAllBlue) return "#0088ff"
                    return root.pairBaseColors[pairIndex]
                }

                // Flash: centre pair when red zone active; all lit LEDs once all-blue kicks in
                readonly property bool shouldFlash: {
                    if (!lit) return false
                    if (root.isAllBlue) return true
                    return pairIndex === (root.ledCount / 2 - 1)
                }

                width:  (root.width - (root.ledCount - 1) * 6) / root.ledCount
                height: root.height
                radius: 4
                color:  ledColor
                border.color: lit ? Qt.darker(ledColor, 1.5) : "#2a2a2a"
                border.width: 1
                opacity: shouldFlash ? root.flashAlpha : 1.0

                Rectangle {
                    visible: parent.lit
                    anchors { top: parent.top; left: parent.left; right: parent.right; margins: 1 }
                    height: parent.height * 0.35
                    radius: parent.radius
                    color:  "white"
                    opacity: 0.18
                }
            }
        }
    }
}
