import QtQuick 2.15

Rectangle {
    id: root

    // 0 = neutral, 1-6 = gear, -1 = reverse
    property int gear: 0

    readonly property string displayText:
        gear < 0 ? "R" : gear === 0 ? "N" : gear.toString()

    readonly property color gearColor:
        gear < 0 ? "#ff3333" : gear === 0 ? "#555555" : "#ffffff"

    color: "#111111"
    border.color: "#2a2a2a"
    border.width: 2
    radius: 3

    Text {
        anchors.top: parent.top
        anchors.topMargin: 12
        anchors.horizontalCenter: parent.horizontalCenter
        text: "GEAR"
        color: "#888888"
        font.pixelSize: 16
        font.letterSpacing: 3
    }

    Text {
        anchors.centerIn: parent
        text: root.displayText
        color: root.gearColor
        font.pixelSize: 120
        font.bold: true
        font.family: "monospace"
    }
}
