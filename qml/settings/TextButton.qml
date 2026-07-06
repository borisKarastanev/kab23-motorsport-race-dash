import QtQuick 2.15

// The dark-HUD "pill" button repeated across the settings pages: a bordered
// Rectangle whose fill flips on press, with centered bold monospace text.
// Callers set border.color / width / enabled / opacity directly (it is a
// Rectangle) and provide text, textColor, pressedColor and onClicked.
Rectangle {
    id: btn

    property alias text: label.text
    property color textColor: "#cccccc"
    property color pressedColor: "#111111"
    signal clicked()

    implicitWidth: label.width + 24
    implicitHeight: 30
    width: implicitWidth
    height: implicitHeight
    radius: 2
    border.width: 1
    color: area.pressed ? pressedColor : "transparent"

    Text {
        id: label
        anchors.centerIn: parent
        color: btn.textColor
        font.pixelSize: 11
        font.bold: true
        font.family: "monospace"
        font.letterSpacing: 1
    }

    MouseArea {
        id: area
        anchors.fill: parent
        enabled: btn.enabled
        onClicked: btn.clicked()
    }
}
