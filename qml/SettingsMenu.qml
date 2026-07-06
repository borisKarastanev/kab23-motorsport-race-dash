import QtQuick 2.15

Item {
    id: menuRoot

    signal itemSelected(string key, string title)

    ListModel {
        id: menuModel
        ListElement { itemTitle: "CONFIGURE UI";       itemKey: "ui" }
        ListElement { itemTitle: "INTEGRATIONS";       itemKey: "integrations" }
        ListElement { itemTitle: "SESSIONS";           itemKey: "sessions" }
        ListElement { itemTitle: "TRACKS";             itemKey: "tracks" }
        ListElement { itemTitle: "DEVICE SETTINGS";    itemKey: "device" }
    }

    // ── header ───────────────────────────────────────────────
    Rectangle {
        id: menuHeader
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: 44
        color: "#0d0d0d"
        border.color: "#2a2a2a"
        border.width: 1

        Text {
            anchors.centerIn: parent
            text: "SETTINGS"
            color: "#888888"
            font.pixelSize: 11
            font.family: "monospace"
            font.letterSpacing: 3
        }
    }

    // ── list ─────────────────────────────────────────────────
    ListView {
        anchors.top: menuHeader.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        model: menuModel
        clip: true

        delegate: Item {
            width: ListView.view.width
            height: 60

            Rectangle {
                anchors.fill: parent
                color: delegateArea.pressed ? "#111111" : "#0d0d0d"

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
                    id: delegateArea
                    anchors.fill: parent
                    onClicked: itemSelected(itemKey, itemTitle)
                }
            }

            // separator line
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
