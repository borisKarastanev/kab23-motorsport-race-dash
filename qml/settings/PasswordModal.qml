import QtQuick 2.15
import RaceDash 1.0

Item {
    id: root
    anchors.fill: parent
    visible: false
    z: 100

    property string enteredPassword: ""

    readonly property bool verifying: updateModel.state === Updates.ValidatingPassword
    readonly property bool wrongPass: updateModel.state === Updates.WrongPassword

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
        width: Math.min(420, root.width - 40)
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
                text: "ADMINISTRATOR PASSWORD REQUIRED"
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
                text: "INCORRECT PASSWORD — TRY AGAIN"
                color: "#cc4444"
                font.pixelSize: 10
                font.family: "monospace"
                font.letterSpacing: 1
            }

            Text {
                visible: root.verifying
                text: "VERIFYING…"
                color: "#888888"
                font.pixelSize: 10
                font.family: "monospace"
                font.letterSpacing: 1
            }

            OnScreenKeyboard {
                width: parent.width
                opacity: root.verifying ? 0.5 : 1.0
                enabled: !root.verifying

                onKeyPressed: function(ch) { root.enteredPassword += ch }
                onBackspace: {
                    if (root.enteredPassword.length > 0)
                        root.enteredPassword = root.enteredPassword.slice(0, -1)
                }
                onDone: {
                    if (root.enteredPassword.length > 0)
                        updateModel.submitPassword(root.enteredPassword)
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
                    enabled: !root.verifying
                    onClicked: updateModel.cancelPassword()
                }

                TextButton {
                    width: (parent.width - 8) / 2
                    text: "SUBMIT"
                    textColor: "#00cc44"
                    pressedColor: "#112211"
                    border.color: "#2a4a2a"
                    opacity: root.enteredPassword.length > 0 && !root.verifying ? 1.0 : 0.5
                    enabled: root.enteredPassword.length > 0 && !root.verifying
                    onClicked: updateModel.submitPassword(root.enteredPassword)
                }
            }
        }
    }
}
