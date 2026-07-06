import QtQuick 2.15
import QtQuick.Layouts 1.15

// A label/value row for the settings stat lists: dim left-aligned label,
// bold right-aligned value, thin bottom divider. Bound directly to model
// properties by the caller so only the changed row updates (no delegate churn).
Rectangle {
    property string label: ""
    property string value: ""
    property color valueColor: "#cccccc"

    Layout.fillWidth: true
    height: 40
    color: "#0d0d0d"

    Text {
        anchors.left: parent.left
        anchors.leftMargin: 16
        anchors.verticalCenter: parent.verticalCenter
        text: label
        color: "#444444"
        font.pixelSize: 9
        font.family: "monospace"
        font.letterSpacing: 2
    }

    Text {
        anchors.right: parent.right
        anchors.rightMargin: 16
        anchors.verticalCenter: parent.verticalCenter
        text: value
        color: valueColor
        font.pixelSize: 12
        font.bold: true
        font.family: "monospace"
        font.letterSpacing: 1
    }

    Rectangle {
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: 16
        anchors.rightMargin: 16
        height: 1
        color: "#1a1a1a"
    }
}
