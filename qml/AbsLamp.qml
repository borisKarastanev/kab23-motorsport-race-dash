pragma ComponentBehavior: Bound
import QtQuick 2.15

// Footer ABS indicator, two looks:
//  - ABS regulating (CanDataModel's derived absActive): a row of purple LEDs
//    pulsing together, no label.
//  - ABS fault (absFault, the MK60's warning lamp): one steady red lamp
//    labelled "ABS". Fault wins, since it means ABS may not be there at all.
// Hidden otherwise.
Item {
    id: root

    property bool active: false
    property bool fault:  false
    // False while the dashboard is swiped away, like Gauge.pulseEnabled, so
    // the infinite pulse doesn't repaint an off-screen footer.
    property bool pulseEnabled: true
    readonly property bool ledMode: active && !fault
    readonly property int  ledCount: 3
    property alias pulseAlpha: pulse.alpha
    // ABS regulating is information, not a warning, so it gets its own colour
    // rather than AlertPulse's amber — amber stays reserved for real warnings.
    readonly property color ledColor: "#b44dff"

    AlertPulse {
        id: pulse
        // Fast danger-rate blink: ABS events are short, the driver gets a glance.
        danger: true
        active: root.visible && root.ledMode && root.pulseEnabled
    }

    visible: active || fault
    implicitWidth:  fault ? badge.implicitWidth  : leds.implicitWidth
    implicitHeight: fault ? badge.implicitHeight : leds.implicitHeight

    Row {
        id: leds
        objectName: "absLeds"
        anchors.centerIn: parent
        spacing: 6
        visible: root.ledMode
        // Pulses the way the RPM strip flashes: the whole LED dims.
        opacity: root.pulseAlpha

        Repeater {
            model: root.ledCount
            delegate: Led {
                width: 20
                height: 12
                radius: 3
                ledColor: root.ledColor
            }
        }
    }

    Rectangle {
        id: badge
        objectName: "absFaultBadge"
        anchors.centerIn: parent
        visible: root.fault
        implicitWidth: label.implicitWidth + 10
        implicitHeight: 16
        radius: 2
        color: pulse.fillDanger
        border.color: pulse.alertDanger
        border.width: 1

        Text {
            id: label
            anchors.centerIn: parent
            text: "ABS"
            color: "#ffffff"
            font.pixelSize: 11
            font.letterSpacing: 1
            font.family: "monospace"
            font.bold: true
        }
    }
}
