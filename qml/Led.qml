import QtQuick 2.15

// One LED in the RPM-strip style: rounded rect, darker rim, faint top
// highlight. Shared by LedStrip and AbsLamp so the two can't drift apart.
Rectangle {
    id: led

    property color ledColor: "#181818"
    property bool  lit: true

    radius: 4
    color: lit ? ledColor : "#181818"
    border.color: lit ? Qt.darker(ledColor, 1.5) : "#2a2a2a"
    border.width: 1

    Rectangle {
        visible: led.lit
        anchors { top: parent.top; left: parent.left; right: parent.right; margins: 1 }
        height: led.height * 0.35
        radius: led.radius
        color:  "white"
        opacity: 0.18
    }
}
