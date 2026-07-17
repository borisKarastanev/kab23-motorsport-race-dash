import QtQuick 2.15
import QtQuick.Layouts 1.15

// A "label  [ − ]  value  [ + ]" row used by Dashboard Layout cards for
// discrete numeric controls (temp thresholds, RPM limiter). Purely
// presentational: value is caller-bound (read-only display), and the caller
// wires decrementRequested/incrementRequested to whatever model setter should
// react — StepperRow has no opinion on clamping or units.
RowLayout {
    id: root

    property string label: ""
    property string value: ""

    signal decrementRequested()
    signal incrementRequested()

    Layout.fillWidth: true
    spacing: 10

    Text {
        text: root.label
        color: "#666666"
        font.pixelSize: 10
        font.family: "monospace"
        font.letterSpacing: 1
    }

    Item { Layout.fillWidth: true }

    Rectangle {
        Layout.preferredWidth: 28
        Layout.preferredHeight: 28
        radius: 2
        color: minusArea.pressed ? "#1a1a1a" : "#111111"
        border.color: "#2a2a2a"
        border.width: 1

        Text {
            anchors.centerIn: parent
            text: "−"
            color: "#cccccc"
            font.pixelSize: 14
            font.bold: true
        }

        MouseArea {
            id: minusArea
            anchors.fill: parent
            onClicked: root.decrementRequested()
        }
    }

    Text {
        Layout.preferredWidth: 56
        horizontalAlignment: Text.AlignHCenter
        text: root.value
        color: "#ffffff"
        font.pixelSize: 13
        font.bold: true
        font.family: "monospace"
    }

    Rectangle {
        Layout.preferredWidth: 28
        Layout.preferredHeight: 28
        radius: 2
        color: plusArea.pressed ? "#1a1a1a" : "#111111"
        border.color: "#2a2a2a"
        border.width: 1

        Text {
            anchors.centerIn: parent
            text: "+"
            color: "#cccccc"
            font.pixelSize: 14
            font.bold: true
        }

        MouseArea {
            id: plusArea
            anchors.fill: parent
            onClicked: root.incrementRequested()
        }
    }
}
