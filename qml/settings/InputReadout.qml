import QtQuick 2.15

// Live value text on the INPUTS screen; dims when the signal isn't live.
Text {
    property bool valid: true

    color: valid ? "#cccccc" : "#555555"
    font.pixelSize: 16
    font.bold: true
    font.family: "monospace"
}
