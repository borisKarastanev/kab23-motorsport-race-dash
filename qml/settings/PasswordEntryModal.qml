import QtQuick 2.15

// Generic full-screen password-entry modal: dim backdrop, masked-dots field,
// on-screen keyboard and CANCEL/SUBMIT buttons. Kept free of any specific
// backend so both the sudo/update flow (PasswordModal) and the Wi-Fi join
// flow (WifiPasswordModal) can drive it via properties + signals. The parent
// controls `visible`; this only reports what the user typed.
Item {
    id: root
    anchors.fill: parent
    visible: false
    z: 100

    property string title: ""
    property bool busy: false
    property string busyLabel: "WORKING…"
    property bool wrongPass: false
    property string wrongLabel: "INCORRECT PASSWORD — TRY AGAIN"

    signal submitted(string password)
    signal cancelled()

    property string enteredPassword: ""

    onVisibleChanged: if (visible) enteredPassword = ""

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
        // 560 fits OnScreenKeyboard's widest row (10 keys + spacing) plus
        // margins at its current key sizes — bump this if those change.
        width: Math.min(560, root.width - 40)
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

            Rectangle {
                width: parent.width
                height: 34
                color: "#111111"
                border.color: root.wrongPass ? "#cc4444" : "#2a2a2a"
                border.width: 1
                radius: 2

                Text {
                    anchors.left: parent.left
                    anchors.leftMargin: 10
                    anchors.verticalCenter: parent.verticalCenter
                    text: "•".repeat(root.enteredPassword.length)
                    color: "#cccccc"
                    font.pixelSize: 16
                    font.family: "monospace"
                }
            }

            Text {
                visible: root.wrongPass
                text: root.wrongLabel
                color: "#cc4444"
                font.pixelSize: 10
                font.family: "monospace"
                font.letterSpacing: 1
            }

            Text {
                visible: root.busy
                text: root.busyLabel
                color: "#888888"
                font.pixelSize: 10
                font.family: "monospace"
                font.letterSpacing: 1
            }

            OnScreenKeyboard {
                width: parent.width
                opacity: root.busy ? 0.5 : 1.0
                enabled: !root.busy

                onKeyPressed: function(ch) { root.enteredPassword += ch }
                onBackspace: {
                    if (root.enteredPassword.length > 0)
                        root.enteredPassword = root.enteredPassword.slice(0, -1)
                }
                onDone: {
                    if (root.enteredPassword.length > 0)
                        root.submitted(root.enteredPassword)
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
                    enabled: !root.busy
                    onClicked: root.cancelled()
                }

                TextButton {
                    width: (parent.width - 8) / 2
                    text: "SUBMIT"
                    textColor: "#00cc44"
                    pressedColor: "#112211"
                    border.color: "#2a4a2a"
                    opacity: root.enteredPassword.length > 0 && !root.busy ? 1.0 : 0.5
                    enabled: root.enteredPassword.length > 0 && !root.busy
                    onClicked: root.submitted(root.enteredPassword)
                }
            }
        }
    }
}
