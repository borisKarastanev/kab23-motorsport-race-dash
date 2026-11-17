import QtQuick 2.15

// Idle auto-shutdown countdown (PowerModel). Shown with the engine off after a
// minute without any interaction; a tap anywhere or a key press cancels it.
// Owns the cancel (PowerModel's filter deliberately doesn't), and covers the
// whole window, so the cancelling tap can't also press the button beneath.
Rectangle {
    id: root

    property int  secondsRemaining: 0
    // Countdown ran out — saving and powering off, no longer cancellable.
    property bool shuttingDown: false

    signal cancelRequested()

    component CardText: Text {
        width: parent.width
        horizontalAlignment: Text.AlignHCenter
        color: "#ffffff"
        font.family: "monospace"
        wrapMode: Text.WordWrap
    }

    color: Qt.rgba(0, 0, 0, 0.75)

    // Non-animating — just the shared alert palette.
    AlertPulse { id: alertPalette }

    focus: visible
    Keys.onPressed: (event) => { root.cancelRequested(); event.accepted = true }

    MouseArea {
        anchors.fill: parent
        onPressed: root.cancelRequested()
    }

    Rectangle {
        anchors.centerIn: parent
        width: Math.min(parent.width - 48, 600)
        height: content.implicitHeight + 64
        radius: 12
        color: "#1a1a1a"
        border.color: alertPalette.alertWarning
        border.width: 3

        Column {
            id: content
            anchors.centerIn: parent
            width: parent.width - 48
            spacing: 16

            CardText {
                text: "ENGINE OFF"
                font.pixelSize: 40
                font.bold: true
                color: alertPalette.alertWarning
            }

            CardText {
                visible: !root.shuttingDown
                text: "Dash shuts down in"
                font.pixelSize: 26
            }

            CardText {
                objectName: "countdownText"
                text: root.shuttingDown ? "Saving session & shutting down…"
                                        : root.secondsRemaining + " s"
                font.pixelSize: root.shuttingDown ? 30 : 96
                font.bold: true
            }

            CardText {
                visible: !root.shuttingDown
                text: "Tap anywhere to keep the dash on"
                font.pixelSize: 22
                color: "#aaaaaa"
            }
        }
    }
}
