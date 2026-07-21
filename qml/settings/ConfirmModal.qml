import QtQuick 2.15

// Generic full-screen confirm/cancel modal: dim backdrop plus a centered
// panel with a title, an optional message, and CANCEL/CONFIRM buttons. Kept
// free of any specific backend, mirroring PasswordEntryModal — the parent
// controls `visible` and reacts to confirmed()/cancelled(). Composes the
// shared ModalScaffold (TICKET-modal-scaffold-extraction).
ModalScaffold {
    id: root
    maxPanelWidth: 360

    property string title: ""
    property string message: ""
    property string confirmLabel: "CONFIRM"
    property string cancelLabel: "CANCEL"

    signal confirmed()
    signal cancelled()

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
