import QtQuick 2.15

// Settings > Device Settings > Date & Time > Time Zone — region drill-in.
// Same delegate shape as DeviceInfoMenu.qml.
Item {
    id: root
    property var stackView: null

    ListView {
        anchors.fill: parent
        model: timeModel.regions
        clip: true

        delegate: Item {
            width: ListView.view.width
            height: 48

            Rectangle {
                anchors.fill: parent
                color: rowArea.pressed ? "#111111" : "#0d0d0d"

                Text {
                    anchors.left: parent.left
                    anchors.leftMargin: 16
                    anchors.verticalCenter: parent.verticalCenter
                    text: modelData
                    color: "#cccccc"
                    font.pixelSize: 12
                    font.family: "monospace"
                    font.letterSpacing: 1
                }

                Text {
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
                    onClicked: root.stackView.push("qrc:/qml/SettingsDetail.qml", {
                        settingKey: "timezone-zones",
                        title: modelData,
                        payload: { region: modelData }
                    })
                }
            }

            Rectangle {
                anchors.bottom: parent.bottom
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.leftMargin: 16
                anchors.rightMargin: 16
                height: 1
                color: "#1a1a1a"
            }
        }
    }
}
