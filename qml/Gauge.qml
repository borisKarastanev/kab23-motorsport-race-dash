import QtQuick 2.15

Rectangle {
    id: root

    property real   value:            0
    property real   maxValue:         100
    property string label:            ""
    property string unit:             ""
    property int    decimalPlaces:    0
    property real   warningThreshold: 999999
    property real   dangerThreshold:  999999

    readonly property bool   isWarning:     value >= warningThreshold && value < dangerThreshold
    readonly property bool   isDanger:      value >= dangerThreshold
    readonly property real   fillRatio:     Qt.clamp(value / maxValue, 0, 1)
    readonly property string formattedValue: value.toFixed(decimalPlaces)

    // Shared accent colours — border and fill bar use the same bright shades
    readonly property color alertDanger:  "#ff3333"
    readonly property color alertWarning: "#ffd700"

    function stateColor(danger, warning, normal) {
        return isDanger ? danger : isWarning ? warning : normal
    }

    color:        stateColor("#8b0000", "#b8860b", "#111111")
    border.color: stateColor(alertDanger, alertWarning, "#2a2a2a")
    border.width: 2
    radius: 3

    Text {
        anchors.top: parent.top
        anchors.topMargin: 12
        anchors.horizontalCenter: parent.horizontalCenter
        text: root.label
        color: "#888888"
        font.pixelSize: 16
        font.letterSpacing: 3
    }

    Text {
        anchors.centerIn: parent
        anchors.verticalCenterOffset: -8
        text: root.formattedValue
        color: "#ffffff"
        font.pixelSize: 72
        font.bold: true
        font.family: "monospace"
    }

    // Declared before Unit text — avoids a forward-anchor layout pass on every update
    Rectangle {
        id: valueBar
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottomMargin: 2
        anchors.leftMargin: 2
        anchors.rightMargin: 2
        height: 5
        color: "#1a1a1a"
        radius: 2

        Rectangle {
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            width: parent.width * root.fillRatio
            color: root.stateColor(root.alertDanger, root.alertWarning, "#00cc44")
            radius: 2
        }
    }

    Text {
        anchors.bottom: valueBar.top
        anchors.bottomMargin: 8
        anchors.horizontalCenter: parent.horizontalCenter
        text: root.unit
        color: root.isWarning || root.isDanger ? "#cccccc" : "#555555"
        font.pixelSize: 15
        font.letterSpacing: 1
    }
}
