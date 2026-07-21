import QtQuick 2.15

// Generic full-screen confirm/cancel modal: dim backdrop plus a centered
// panel with a title, an optional message, and CANCEL/CONFIRM buttons. Kept
// free of any specific backend, mirroring PasswordEntryModal — the parent
// controls `visible` and reacts to confirmed()/cancelled().
Item {
    id: root
    anchors.fill: parent
    visible: false
    z: 100

    property string title: ""
    property string message: ""
    property string confirmLabel: "CONFIRM"
    property string cancelLabel: "CANCEL"

    signal confirmed()
    signal cancelled()

    // dim backdrop — swallows all clicks behind the modal
    Rectangle {
        anchors.fill: parent
        color: "#000000"
        opacity: 0.7

        MouseArea {
            anchors.fill: parent
            onClicked: {} // absorb clicks
        }
    }

    Rectangle {
        id: panel
        anchors.centerIn: parent
        width: Math.min(360, root.width - 40)
        height: contentColumn.height + 32
        color: "#0d0d0d"
        border.color: "#2a2a2a"
        border.width: 1
        radius: 3

        MouseArea { anchors.fill: parent; onClicked: {} } // absorb clicks

        Column {
            id: contentColumn
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.margins: 16
            spacing: 12

            Text {
                text: root.title
                color: "#cccccc"
                font.pixelSize: 12
                font.bold: true
                font.family: "monospace"
                font.letterSpacing: 1
                wrapMode: Text.WordWrap
                width: parent.width
            }

            Text {
                visible: root.message.length > 0
                text: root.message
                color: "#888888"
                font.pixelSize: 11
                font.family: "monospace"
                wrapMode: Text.WordWrap
                width: parent.width
            }

            Row {
                width: parent.width
                spacing: 8

                TextButton {
                    width: (parent.width - 8) / 2
                    text: root.cancelLabel
                    textColor: "#888888"
                    pressedColor: "#111111"
                    border.color: "#2a2a2a"
                    onClicked: root.cancelled()
                }

                TextButton {
                    width: (parent.width - 8) / 2
                    text: root.confirmLabel
                    textColor: "#cc4444"
                    pressedColor: "#221111"
                    border.color: "#4a2a2a"
                    onClicked: root.confirmed()
                }
            }
        }
    }
}
