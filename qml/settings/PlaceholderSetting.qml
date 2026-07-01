import QtQuick 2.15

Item {
    property string title: ""

    Text {
        anchors.centerIn: parent
        text: title.length > 0 ? title + "\nNOT YET IMPLEMENTED" : "NOT YET IMPLEMENTED"
        color: "#cccccc"
        font.pixelSize: 12
        font.bold: true
        font.family: "monospace"
        font.letterSpacing: 2
        horizontalAlignment: Text.AlignHCenter
        lineHeight: 1.8
    }
}
