import QtQuick 2.15
import QtQuick.Layouts 1.15
import RaceDash 1.0

Item {
    id: root

    Component.onCompleted: updateModel.checkConnection()
    Component.onDestruction: {
        if (updateModel.state !== Updates.Installing && updateModel.state !== Updates.Rebooting)
            updateModel.reset()
    }

    readonly property int state: updateModel.state

    // State groupings the UI switches on, named once so each binding isn't a
    // fresh array literal and the membership lives in a single place.
    readonly property bool checkGroup: [Updates.ReadyToCheck, Updates.Checking,
                                        Updates.CheckFailed, Updates.UpToDate].indexOf(state) !== -1
    readonly property bool installGroup: [Updates.UpdateAvailable, Updates.AwaitingPassword,
                                          Updates.ValidatingPassword, Updates.WrongPassword].indexOf(state) !== -1
    readonly property bool passwordFlow: [Updates.AwaitingPassword, Updates.ValidatingPassword,
                                          Updates.WrongPassword].indexOf(state) !== -1

    // ── Rebooting: full-screen warning, nothing else matters ───────
    Rectangle {
        anchors.fill: parent
        visible: root.state === Updates.Rebooting
        color: "#1a1200"

        Text {
            anchors.centerIn: parent
            width: parent.width - 40
            text: "REBOOTING…\n\nDO NOT SWITCH OFF THE ENGINE\nUNTIL THE DASHBOARD IS BACK UP"
            color: "#cc8800"
            font.pixelSize: 14
            font.bold: true
            font.family: "monospace"
            font.letterSpacing: 1
            horizontalAlignment: Text.AlignHCenter
            lineHeight: 1.6
            wrapMode: Text.WordWrap
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 24
        spacing: 18
        visible: root.state !== Updates.Rebooting

        Text {
            text: "VERSION " + updateModel.currentVersion
            color: "#888888"
            font.pixelSize: 12
            font.family: "monospace"
            font.letterSpacing: 2
        }

        // ── CheckingConnection ───────────────────────────────────
        Text {
            visible: root.state === Updates.CheckingConnection
            text: "CHECKING CONNECTION…"
            color: "#888888"
            font.pixelSize: 11
            font.family: "monospace"
            font.letterSpacing: 1
        }

        // ── NoConnection ─────────────────────────────────────────
        ColumnLayout {
            visible: root.state === Updates.NoConnection
            spacing: 10

            Text {
                Layout.fillWidth: true
                text: "INTERNET CONNECTION REQUIRED TO CHECK FOR UPDATES"
                color: "#888888"
                font.pixelSize: 11
                font.family: "monospace"
                font.letterSpacing: 1
                wrapMode: Text.WordWrap
            }

            TextButton {
                text: "⟳ RETRY"
                textColor: "#4488cc"
                pressedColor: "#111122"
                border.color: "#2a2a4a"
                onClicked: updateModel.checkConnection()
            }
        }

        // ── ReadyToCheck / Checking / CheckFailed / UpToDate ────
        ColumnLayout {
            visible: root.checkGroup
            spacing: 10

            Text {
                visible: root.state === Updates.CheckFailed
                text: "CHECK FAILED: " + updateModel.errorText
                color: "#cc4444"
                font.pixelSize: 10
                font.family: "monospace"
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }

            Text {
                visible: root.state === Updates.UpToDate
                text: "YOU HAVE THE LATEST VERSION"
                color: "#00cc44"
                font.pixelSize: 12
                font.bold: true
                font.family: "monospace"
                font.letterSpacing: 1
            }

            TextButton {
                text: root.state === Updates.Checking ? "CHECKING…"
                    : root.state === Updates.UpToDate  ? "CHECK AGAIN"
                    : root.state === Updates.CheckFailed ? "⟳ RETRY"
                    : "CHECK FOR UPDATES"
                textColor: "#4488cc"
                pressedColor: "#111122"
                border.color: "#2a2a4a"
                opacity: root.state === Updates.Checking ? 0.5 : 1.0
                enabled: root.state !== Updates.Checking
                onClicked: updateModel.checkForUpdates()
            }
        }

        // ── UpdateAvailable + password flow ──────────────────────
        ColumnLayout {
            visible: root.installGroup
            spacing: 10
            opacity: root.state === Updates.UpdateAvailable ? 1.0 : 0.4

            Text {
                text: "UPDATE AVAILABLE: " + updateModel.latestVersion
                color: "#00cc44"
                font.pixelSize: 12
                font.bold: true
                font.family: "monospace"
                font.letterSpacing: 1
            }

            TextButton {
                text: "INSTALL"
                textColor: "#00cc44"
                pressedColor: "#112211"
                border.color: "#2a4a2a"
                enabled: root.state === Updates.UpdateAvailable
                onClicked: updateModel.startInstall()
            }
        }

        // ── Installing ────────────────────────────────────────────
        ColumnLayout {
            visible: root.state === Updates.Installing
            spacing: 10
            Layout.fillWidth: true

            Text {
                text: updateModel.installStage
                color: "#cccccc"
                font.pixelSize: 12
                font.family: "monospace"
                font.letterSpacing: 1
            }

            Rectangle {
                Layout.fillWidth: true
                height: 10
                color: "#111111"
                border.color: "#2a2a2a"
                border.width: 1
                radius: 2

                Rectangle {
                    anchors.left: parent.left
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    width: parent.width * updateModel.installProgress
                    color: "#00cc44"
                    radius: 2
                }
            }

            Text {
                text: "DO NOT POWER OFF"
                color: "#cc8800"
                font.pixelSize: 11
                font.bold: true
                font.family: "monospace"
                font.letterSpacing: 1
            }
        }

        // ── InstallFailed ─────────────────────────────────────────
        ColumnLayout {
            visible: root.state === Updates.InstallFailed
            spacing: 10
            Layout.fillWidth: true

            Text {
                Layout.fillWidth: true
                text: "UPDATE FAILED: " + updateModel.errorText
                color: "#cc4444"
                font.pixelSize: 10
                font.family: "monospace"
                wrapMode: Text.WordWrap
            }

            TextButton {
                text: "BACK"
                textColor: "#888888"
                pressedColor: "#111111"
                border.color: "#2a2a2a"
                onClicked: updateModel.reset()
            }
        }

        // ── InstallSucceeded ──────────────────────────────────────
        ColumnLayout {
            visible: root.state === Updates.InstallSucceeded
            spacing: 12
            Layout.fillWidth: true

            Text {
                text: "UPDATE INSTALLED"
                color: "#00cc44"
                font.pixelSize: 12
                font.bold: true
                font.family: "monospace"
                font.letterSpacing: 1
            }

            Text {
                text: "RESTART REQUIRED TO APPLY THE UPDATE"
                color: "#cccccc"
                font.pixelSize: 11
                font.family: "monospace"
                font.letterSpacing: 1
            }

            Text {
                Layout.fillWidth: true
                text: "WARNING: DO NOT SWITCH OFF THE ENGINE UNTIL THE DASHBOARD IS BACK UP"
                color: "#cc8800"
                font.pixelSize: 11
                font.bold: true
                font.family: "monospace"
                font.letterSpacing: 1
                wrapMode: Text.WordWrap
            }

            Row {
                spacing: 8

                TextButton {
                    text: "LATER"
                    textColor: "#888888"
                    pressedColor: "#111111"
                    border.color: "#2a2a2a"
                    onClicked: {
                        updateModel.reset()
                        updateModel.checkConnection()
                    }
                }

                TextButton {
                    text: "RESTART NOW"
                    textColor: "#cc8800"
                    pressedColor: "#221100"
                    border.color: "#4a3a2a"
                    onClicked: updateModel.requestReboot()
                }
            }
        }

        Item { Layout.fillHeight: true }
    }

    PasswordModal {
        visible: root.passwordFlow
    }
}
