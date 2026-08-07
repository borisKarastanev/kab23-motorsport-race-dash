import QtQuick 2.15

// Full-screen plain-text entry: dim backdrop, visible field, on-screen keyboard
// and CANCEL/SUBMIT. The unmasked sibling of PasswordEntryModal, sharing the
// same ModalScaffold and OnScreenKeyboard.
//
// A separate component rather than a `masked` flag on PasswordEntryModal: that
// modal's whole job is to be the one place a secret is typed, and giving it a
// property that turns masking off invites exactly the mistake it exists to
// prevent. Here the text is meant to be read back — a broker hostname typed
// blind on a touchscreen in a pit lane is a support call.
ModalScaffold {
    id: root
    maxPanelWidth: 560

    property string title: ""
    // Prefills the field when the modal opens, so editing an existing value is
    // not a retype-from-scratch.
    property string seed: ""

    signal submitted(string text)
    signal cancelled()

    property string enteredText: ""

    onVisibleChanged: if (visible) enteredText = seed

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

    Rectangle {
        width: parent.width
        height: 34
        color: "#111111"
        border.color: "#2a2a2a"
        border.width: 1
        radius: 2

        Text {
            anchors.left: parent.left
            anchors.leftMargin: 10
            anchors.right: parent.right
            anchors.rightMargin: 10
            anchors.verticalCenter: parent.verticalCenter
            text: root.enteredText
            elide: Text.ElideLeft   // a long hostname keeps its tail visible
            color: "#cccccc"
            font.pixelSize: 16
            font.family: "monospace"
        }
    }

    OnScreenKeyboard {
        width: parent.width

        onKeyPressed: function(ch) { root.enteredText += ch }
        onBackspace: {
            if (root.enteredText.length > 0)
                root.enteredText = root.enteredText.slice(0, -1)
        }
        onDone: {
            if (root.enteredText.length > 0)
                root.submitted(root.enteredText)
        }
    }

    Row {
        width: parent.width
        spacing: 8

        TextButton {
            width: (parent.width - 8) / 2
            text: "CANCEL"
            textColor: "#888888"
            pressedColor: "#111111"
            border.color: "#2a2a2a"
            onClicked: root.cancelled()
        }

        TextButton {
            width: (parent.width - 8) / 2
            text: "SUBMIT"
            textColor: "#00cc44"
            pressedColor: "#112211"
            border.color: "#2a4a2a"
            opacity: root.enteredText.length > 0 ? 1.0 : 0.5
            enabled: root.enteredText.length > 0
            onClicked: root.submitted(root.enteredText)
        }
    }
}
