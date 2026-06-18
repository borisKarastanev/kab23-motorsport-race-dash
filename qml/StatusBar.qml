import QtQuick 2.15
import QtQuick.Layouts 1.15

Rectangle {
    color: "#0d0d0d"
    border.color: "#1a1a1a"
    border.width: 1

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

        // Spacer
        Item { Layout.fillWidth: true }

        // Finish line button
        Rectangle {
            height: 20
            width: finishLineLabel.width + 16
            color: mouseArea.pressed ? "#1a2a1a" : "transparent"
            border.color: raceBoxModel.finishLineSet ? "#2a4a2a" : "#333333"
            border.width: 1
            radius: 2
            anchors.verticalCenter: parent.verticalCenter

            Text {
                id: finishLineLabel
                anchors.centerIn: parent
                text: raceBoxModel.finishLineSet ? "✓ FINISH LINE SET" : "+ SET FINISH LINE"
                color: raceBoxModel.finishLineSet ? "#00cc44" : "#555555"
                font.pixelSize: 10
                font.letterSpacing: 1
                font.family: "monospace"
            }

            MouseArea {
                id: mouseArea
                anchors.fill: parent
                enabled: raceBoxModel.hasFix
                onClicked: raceBoxModel.learnFinishLineHere()
            }
        }
    }
}
