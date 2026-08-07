import QtQuick 2.15
import QtQuick.Layouts 1.15

// The settings-page on/off row: dim monospace label on the left, pill switch on
// the right, thin bottom divider — the same geometry and inset as StatRow, so a
// toggle sitting in a stat list lines up with the rows around it.
//
// Shared rather than copied because a switch is how this UI says "is this radio
// on": it reads as current state at a glance, where a button labelled with the
// opposite action ("DISABLE UPLINK" when enabled) has to be decoded first —
// which is the wrong demand to make of somebody glancing at a screen in a pit
// lane. Wi-Fi and the cloud uplink both answer that question, and they must look
// identical doing it.
//
// The caller owns the state: `checked` is bound to the model, and `toggled` is
// what asks the model to change it. Nothing here flips on its own, so a setting
// whose write can fail or lag (both of these go through a system service) shows
// what is true rather than what was tapped.
Rectangle {
    id: row

    property string label: ""
    property bool   checked: false
    // Mid-flight: dims the control and swallows taps, so a second tap cannot
    // queue a contradictory request behind the first.
    property bool   busy: false

    signal toggled()

    // Sized like StatRow: fills a ColumnLayout on its own, and a caller inside a
    // plain Column sets `width: parent.width` as it already does for StatRow.
    Layout.fillWidth: true
    height: 60
    color: "#0d0d0d"

    Text {
        anchors.left: parent.left
        anchors.leftMargin: 16
        anchors.verticalCenter: parent.verticalCenter
        text: row.label
        color: "#cccccc"
        font.pixelSize: 12
        font.bold: true
        font.family: "monospace"
        font.letterSpacing: 2
    }

    Rectangle {
        id: track
        anchors.right: parent.right
        anchors.rightMargin: 16
        anchors.verticalCenter: parent.verticalCenter
        width: 46
        height: 24
        radius: 12
        opacity: row.busy ? 0.5 : 1.0
        color: row.checked ? "#00cc44" : "#2a2a2a"
        border.color: "#1a1a1a"
        border.width: 1

        Rectangle {
            width: 18
            height: 18
            radius: 9
            anchors.verticalCenter: parent.verticalCenter
            color: "#0d0d0d"
            x: row.checked ? (parent.width - width - 3) : 3
            Behavior on x { NumberAnimation { duration: 150 } }
        }

        MouseArea {
            anchors.fill: parent
            enabled: !row.busy
            onClicked: row.toggled()
        }
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
