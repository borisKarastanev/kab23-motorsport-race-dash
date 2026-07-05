import QtQuick 2.15
import QtQuick.Controls.Basic

Item {
    property var stackView: null

    ListView {
        anchors.fill: parent
        model: sessionModel.sessionGroups
        clip: true

        delegate: Item {
            width: ListView.view.width
            height: 60

            Rectangle {
                anchors.fill: parent
                color: rowArea.pressed ? "#111111" : "#0d0d0d"

                Column {
                    anchors.centerIn: parent
                    spacing: 2

                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: modelData.trackName
                        color: "#cccccc"
                        font.pixelSize: 12
                        font.bold: true
                        font.family: "monospace"
                        font.letterSpacing: 2
                    }
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: modelData.latestTitle
                        color: "#555555"
                        font.pixelSize: 9
                        font.family: "monospace"
                        font.letterSpacing: 1
                    }
                }

                Text {
                    anchors.right: arrow.left
                    anchors.rightMargin: 12
                    anchors.verticalCenter: parent.verticalCenter
                    text: modelData.count
                    color: "#444444"
                    font.pixelSize: 11
                    font.family: "monospace"
                }

                Text {
                    id: arrow
                    anchors.right: parent.right
                    anchors.rightMargin: 20
                    anchors.verticalCenter: parent.verticalCenter
                    text: "›"
                    color: "#444444"
                    font.pixelSize: 18
                    font.family: "monospace"
                }

                MouseArea {
                    id: rowArea
                    anchors.fill: parent
                    onClicked: {
                        stackView.push("qrc:/qml/SettingsDetail.qml", {
                            settingKey: "session-track-list",
                            title: modelData.trackName,
                            payload: modelData
                        })
                    }
                }
            }

            Rectangle {
                anchors.bottom: parent.bottom
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.leftMargin: 20
                anchors.rightMargin: 20
                height: 1
                color: "#2a2a2a"
            }
        }

        Text {
            anchors.centerIn: parent
            visible: sessionModel.sessionGroups.length === 0
            text: "NO SAVED SESSIONS"
            color: "#333333"
            font.pixelSize: 12
            font.bold: true
            font.family: "monospace"
            font.letterSpacing: 2
        }
    }
}
