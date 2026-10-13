import QtQuick 2.15

// Live steering-wheel indicator driven by the LWS angle (0x1F5).
Item {
    id: root

    property real angle: 0 // degrees, positive = right
    // False when 0x1F5 is missing or stale — a dimmed wheel and "--" rather
    // than a centred 0.0° that would pass for a calibrated LWS.
    property bool valid: true

    readonly property string angleText: {
        if (!valid)
            return "--"
        const a = Math.abs(angle)
        if (a < 0.05)
            return "0.0°"
        return a.toFixed(1) + "° " + (angle > 0 ? "R" : "L")
    }

    implicitWidth: 260
    implicitHeight: 300

    Item {
        id: wheel
        anchors.top: parent.top
        anchors.horizontalCenter: parent.horizontalCenter
        width: Math.max(0, Math.min(root.width, root.height - readout.height - 12))
        height: width
        opacity: root.valid ? 1.0 : 0.3

        // Bit-15-set decodes negative; positive is assumed to be a right turn.
        // If the car shows the opposite, negate the angle here.
        rotation: root.angle

        readonly property real rim: width * 0.09

        // spokes: left, right, bottom
        Rectangle {
            anchors.verticalCenter: parent.verticalCenter
            x: wheel.rim / 2
            width: wheel.width - wheel.rim
            height: wheel.rim * 0.7
            color: "#3a3a3a"
        }
        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            y: wheel.height / 2
            width: wheel.rim * 0.7
            height: wheel.height / 2 - wheel.rim / 2
            color: "#3a3a3a"
        }

        // rim
        Rectangle {
            anchors.fill: parent
            radius: width / 2
            color: "transparent"
            border.color: "#cccccc"
            border.width: wheel.rim
        }

        // hub
        Rectangle {
            anchors.centerIn: parent
            width: wheel.width * 0.26
            height: width
            radius: width / 2
            color: "#1a1a1a"
            border.color: "#3a3a3a"
            border.width: 2
        }

        // 12 o'clock marker
        Rectangle {
            anchors.top: parent.top
            anchors.horizontalCenter: parent.horizontalCenter
            width: wheel.rim * 0.8
            height: wheel.rim
            color: "#cc3333"
        }
    }

    InputReadout {
        id: readout
        anchors.bottom: parent.bottom
        anchors.horizontalCenter: parent.horizontalCenter
        text: root.angleText
        valid: root.valid
        font.pixelSize: 20
    }
}
