import QtQuick 2.15
import QtQuick.Layouts 1.15
import "settings"

Rectangle {
    color: "#0d0d0d"
    border.color: "#1a1a1a"
    border.width: 1

    signal resetFinishLineRequested()

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 10
        anchors.rightMargin: 10
        spacing: 16

        // RaceBox connection
        Row {
            spacing: 5
            Rectangle {
                width: 8; height: 8
                radius: 4
                anchors.verticalCenter: parent.verticalCenter
                color: raceBoxModel.connected ? "#00cc44" : "#cc8800"
                SequentialAnimation on opacity {
                    running: !raceBoxModel.connected
                    loops: Animation.Infinite
                    NumberAnimation { to: 0.25; duration: 800; easing.type: Easing.InOutSine }
                    NumberAnimation { to: 1.0;  duration: 800; easing.type: Easing.InOutSine }
                }
            }
            Text {
                text: raceBoxModel.connected ? "RACEBOX" : "SCANNING..."
                color: raceBoxModel.connected ? "#448844" : "#886600"
                font.pixelSize: 11
                font.letterSpacing: 1
                font.family: "monospace"
                anchors.verticalCenter: parent.verticalCenter
            }
        }

        // GPS fix + satellites
        Row {
            spacing: 4
            Text {
                text: "\u{1F6F0}"   // 🛰
                font.pixelSize: 12
                anchors.verticalCenter: parent.verticalCenter
                color: raceBoxModel.hasFix ? "#448844" : "#444444"
            }
            Text {
                text: raceBoxModel.hasFix
                      ? raceBoxModel.satellites + " SVs"
                      : "NO FIX"
                color: raceBoxModel.hasFix ? "#448844" : "#444444"
                font.pixelSize: 11
                font.letterSpacing: 1
                font.family: "monospace"
                anchors.verticalCenter: parent.verticalCenter
            }
        }

        // Battery
        Row {
            spacing: 4
            visible: raceBoxModel.connected
            Text {
                text: raceBoxModel.batteryCharging ? "⚡" : "\u{1F50B}"
                font.pixelSize: 12
                anchors.verticalCenter: parent.verticalCenter
                color: raceBoxModel.batteryCharging ? "#4488cc"
                     : raceBoxModel.batteryPercent > 20 ? "#448844" : "#cc4444"
            }
            Text {
                text: raceBoxModel.batteryCharging
                      ? raceBoxModel.batteryPercent + "% ⚡"
                      : raceBoxModel.batteryPercent + "%"
                color: raceBoxModel.batteryCharging ? "#4488cc"
                     : raceBoxModel.batteryPercent > 20 ? "#448844" : "#cc4444"
                font.pixelSize: 11
                font.family: "monospace"
                anchors.verticalCenter: parent.verticalCenter
            }
        }

        // Wi-Fi connected indicator — icon only, no label
        WifiIcon {
            visible: networkModel.wifiConnected
            size: 12
            color: "#448844"
            Layout.alignment: Qt.AlignVCenter
        }

        // Spacer
        Item { Layout.fillWidth: true }

        // Save Session button — visible only when stationary and laps have been recorded
        Rectangle {
            id: saveSessionBtn
            visible: sessionModel.canSave
            height: 20
            width: saveSessionLabel.width + 16
            color: saveSessionArea.pressed ? "#111122" : "transparent"
            border.color: "#2a2a4a"
            border.width: 1
            radius: 2
            Layout.alignment: Qt.AlignVCenter
            onVisibleChanged: if (!visible) saveFlashTimer.stop()

            Text {
                id: saveSessionLabel
                anchors.centerIn: parent
                text: "SAVE SESSION"
                color: "#4488cc"
                font.pixelSize: 10
                font.letterSpacing: 1
                font.family: "monospace"
            }

            MouseArea {
                id: saveSessionArea
                anchors.fill: parent
                onClicked: {
                    sessionModel.saveCurrentSession()
                    saveSessionLabel.text = "✓ SESSION SAVED"
                    saveFlashTimer.restart()
                }
            }

            Timer {
                id: saveFlashTimer
                interval: 1500
                onTriggered: saveSessionLabel.text = "SAVE SESSION"
            }
        }

        // Finish line button. A gate is built perpendicular to the car's travel
        // heading, so it can only be learned once moving — canLearnFinishLine is the
        // model's own precondition for that, and binding to it keeps the button from
        // offering a tap that would silently do nothing. "Has a fix but still can't
        // learn" means the heading is the missing half, so prompt the driver to move.
        Rectangle {
            height: 20
            width: finishLineLabel.width + 16
            color: mouseArea.pressed ? "#1a2a1a" : "transparent"
            border.color: raceBoxModel.finishLineSet     ? "#2a4a2a"
                        : raceBoxModel.canLearnFinishLine ? "#445544"
                        :                                   "#222222"
            border.width: 1
            radius: 2
            Layout.alignment: Qt.AlignVCenter

            Text {
                id: finishLineLabel
                anchors.centerIn: parent
                text: raceBoxModel.finishLineSet
                      ? (mouseArea.pressed ? "⟳ HOLD TO RESET..." : "✓ FINISH LINE SET")
                      : raceBoxModel.canLearnFinishLine ? "+ SET FINISH LINE"
                      : raceBoxModel.hasFix             ? "DRIVE TO SET LINE"
                      :                                   "+ SET FINISH LINE"
                color: raceBoxModel.finishLineSet     ? "#00cc44"
                     : raceBoxModel.canLearnFinishLine ? "#558855"
                     :                                   "#333333"
                font.pixelSize: 10
                font.letterSpacing: 1
                font.family: "monospace"
            }

            MouseArea {
                id: mouseArea
                anchors.fill: parent
                // Two actions share this area: tap-to-set (needs the model to be
                // able to learn) and hold-to-reset (needs a line already set).
                enabled: raceBoxModel.finishLineSet || raceBoxModel.canLearnFinishLine
                pressAndHoldInterval: 3000
                onClicked: {
                    if (!raceBoxModel.finishLineSet)
                        raceBoxModel.learnFinishLineHere()
                }
                onPressAndHold: {
                    if (raceBoxModel.finishLineSet)
                        resetFinishLineRequested()
                }
            }
        }
    }
}
