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
    property bool   compact:          false

    readonly property bool   isWarning:     value >= warningThreshold && value < dangerThreshold
    readonly property bool   isDanger:      value >= dangerThreshold

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
        anchors.topMargin: compact ? 6 : 12
        anchors.horizontalCenter: parent.horizontalCenter
        text: root.label
        color: "#888888"
        font.pixelSize: compact ? 11 : 16
        font.letterSpacing: 3
    }

    Text {
        anchors.centerIn: parent
        anchors.verticalCenterOffset: compact ? -4 : -8
        text: root.formattedValue
        color: "#ffffff"
        font.pixelSize: compact ? 44 : 72
        font.bold: true
        font.family: "monospace"
    }

    Text {
        anchors.bottom: parent.bottom
        anchors.bottomMargin: compact ? 6 : 12
        anchors.horizontalCenter: parent.horizontalCenter
        text: root.unit
        color: root.isWarning || root.isDanger ? "#cccccc" : "#555555"
        font.pixelSize: compact ? 11 : 15
        font.letterSpacing: 1
    }
}
