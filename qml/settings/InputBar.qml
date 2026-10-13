import QtQuick 2.15

// Vertical 0..100 % fill bar for a driver input (pedal position).
Item {
    id: root

    property string label: ""
    property real   value: 0          // 0..100
    property color  fillColor: "#33cc66"
    property bool   valid: true
    property string valueText: valid ? Math.round(clampedValue) + "%" : "--"

    readonly property real clampedValue: Math.max(0, Math.min(100, value))
    readonly property alias fillHeight: fill.height

    implicitWidth: 72

    InputReadout {
        id: valueLabel
        anchors.top: parent.top
        anchors.horizontalCenter: parent.horizontalCenter
        text: root.valueText
        valid: root.valid
    }

    Rectangle {
        id: track
        anchors.top: valueLabel.bottom
        anchors.topMargin: 8
        anchors.bottom: nameLabel.top
        anchors.bottomMargin: 8
        anchors.horizontalCenter: parent.horizontalCenter
        width: 48
        color: "#1a1a1a"
        border.color: "#2a2a2a"
        border.width: 1
        radius: 2
        opacity: root.valid ? 1.0 : 0.4

        Rectangle {
            id: fill
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.margins: 2
            height: root.valid ? (track.height - 4) * root.clampedValue / 100 : 0
            color: root.fillColor
            radius: 1

            // CanDataModel notifies at 10 Hz — ease between steps.
            Behavior on height { NumberAnimation { duration: 100 } }
        }
    }

    InputLabel {
        id: nameLabel
        anchors.bottom: parent.bottom
        anchors.horizontalCenter: parent.horizontalCenter
        text: root.label
    }
}
