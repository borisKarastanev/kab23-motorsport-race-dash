import QtQuick 2.15

Item {
    id: root
    property var stackView: null

    ListModel {
        id: menuModel
        ListElement { itemTitle: "APP VERSION";  itemKey: "device-version" }
        ListElement { itemTitle: "DEVICE STATS";  itemKey: "device-stats" }
        ListElement { itemTitle: "DEVICE LOG";    itemKey: "device-log" }
        ListElement { itemTitle: "NETWORK CONNECTION"; itemKey: "network" }
    }

    ListView {
        anchors.fill: parent
        model: menuModel
        clip: true

        delegate: Item {
            width: ListView.view.width
            height: 60

            Rectangle {
                anchors.fill: parent
                color: rowArea.pressed ? "#111111" : "#0d0d0d"

                Text {
                    anchors.centerIn: parent
                    text: itemTitle
                    color: "#cccccc"
                    font.pixelSize: 12
                    font.bold: true
                    font.family: "monospace"
                    font.letterSpacing: 2
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
                    onClicked: {
                        root.stackView.push("qrc:/qml/SettingsDetail.qml", {
                            settingKey: itemKey,
                            title: itemTitle
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
    }
}
